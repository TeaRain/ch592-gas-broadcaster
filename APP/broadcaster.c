#include "CONFIG.h"
#include "devinfoservice.h"
#include "broadcaster.h"

/* 按键中断标志 */
#define KEY_IRQ_B                 0x01
#define KEY_IRQ_P                 0x02
#define KEY_IRQ_N                 0x04
#define KEY_PB_IRQ_MASK           (GPIO_Pin_10 | GPIO_Pin_11 | (GPIO_Pin_22 >> 14))

/* DataFlash 双备份记录，两个槽位相隔4KB，避免一次擦除影响另一个槽位 */
#define GAS_FLASH_MAGIC           0x47415331UL  /* "GAS1" */
#define GAS_FLASH_SLOT0           0
#define GAS_FLASH_SLOT1           4096

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint32_t value;
    uint32_t crc;          /* 低16位为CRC16 */
} gas_record_t;

static uint8_t Broadcaster_TaskID;

static uint8_t s_ucBroadcasterCnt = 0;    // 广播次数
static uint16_t s_u16BatVolt = 0;         // 电池采样电压
static uint32_t s_u32GasMeter = 0;        // 燃气表数值，0.001立方米/bit
static uint16_t s_u16FirstPowerOn = 200;  // 上电1分钟内不计量燃气
static uint8_t s_bTcrt5000Use = FALSE;    // 是否已建立反射基准
static uint16_t s_u16Tcrt5000Min = 4095;  // 反射差分最小值
static uint16_t s_u16Tcrt5000 = 0;        // 反射差分值
static uint8_t s_bKeyPBlocked = FALSE;    // P键上电已拉低，释放前忽略
static uint8_t s_bKeyNBlocked = FALSE;    // N键上电已拉低，释放前忽略

static uint16_t s_u16KeyDownTime = 0;     // 按键按下时间
static uint16_t s_u16KeyBDownTime = 0;    // B键长按计时（OTA模式）

static uint8_t s_bKeyBPressed = FALSE;    // B键上一次采样状态
static uint8_t s_bAdjustActive = FALSE;   // P/N键正在调整
static volatile uint8_t s_u8KeyIrqMask = 0; // 按键下降沿中断标志

static uint32_t s_u32FlashSeq = 0;        // 记录序号
static uint8_t s_u8FlashSlot = 1;         // 上一次写入的槽位
static uint32_t s_u32LastSaved = 0;       // 上一次保存的燃气值

#define FW_VERSION_LEN (sizeof(FW_VERSION_STRING) - 1)
static uint8_t bthomeData[19 + 2 + FW_VERSION_LEN] =
{
    0x02, 0x01, 0x06,             // Flags data
    0x0F, 0x16, 0xD2, 0xFC, 0x40, // 服务数据bthomev2
    0x0C, 0x00, 0x00,             // 电池电压，0.001V/bit
    0x3D, 0x00, 0x00,             // ADC反射差分，1CNT/bit
    0x4C, 0x00, 0x00, 0x00, 0x00  // 燃气表读数，0.001立方米/bit
};

static void Broadcaster_StateNotificationCB(gapRole_States_t newState);
static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = {Broadcaster_StateNotificationCB, NULL};

// 按键下降沿中断：只记录事件，具体处理放到300ms任务里
__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    if(GPIOB_ReadITFlagBit(GPIO_Pin_22))
    {
        s_u8KeyIrqMask |= KEY_IRQ_B;
    }
    if(GPIOB_ReadITFlagBit(GPIO_Pin_11))
    {
        s_u8KeyIrqMask |= KEY_IRQ_P;
    }
    if(GPIOB_ReadITFlagBit(GPIO_Pin_10))
    {
        s_u8KeyIrqMask |= KEY_IRQ_N;
    }
    GPIOB_ClearITFlagBit(GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_22);
}

// CRC16-CCITT
static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t b;

    for(i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(b = 0; b < 8; b++)
        {
            if(crc & 0x8000)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

// 计算两个燃气值之间的增量，支持满量程回零
static uint32_t gas_delta(uint32_t now, uint32_t saved)
{
    if(now >= saved)
    {
        return now - saved;
    }
    return (GAS_METER_ROLLOVER - saved) + now;
}

// 读取并校验一个槽位
static uint8_t read_gas_record(uint32_t addr, gas_record_t *rec)
{
    EEPROM_READ(addr, (uint8_t *)rec, sizeof(gas_record_t));

    if(rec->magic != GAS_FLASH_MAGIC)
    {
        return FALSE;
    }
    if(rec->crc != (uint32_t)crc16_ccitt((const uint8_t *)rec, 12))
    {
        return FALSE;
    }
    if(rec->value >= GAS_METER_ROLLOVER)
    {
        return FALSE;
    }
    return TRUE;
}

// 写入数据到DataFlash：双槽位交替写，带序号和CRC
void SaveToDataFlash(void)
{
    gas_record_t rec;
    uint8_t slot;
    uint32_t addr;

    slot = s_u8FlashSlot ^ 1;
    addr = (slot == 0) ? GAS_FLASH_SLOT0 : GAS_FLASH_SLOT1;

    rec.magic = GAS_FLASH_MAGIC;
    rec.seq = ++s_u32FlashSeq;
    rec.value = s_u32GasMeter;
    rec.crc = (uint32_t)crc16_ccitt((const uint8_t *)&rec, 12);

    EEPROM_ERASE(addr, EEPROM_BLOCK_SIZE);
    EEPROM_WRITE(addr, (uint8_t *)&rec, sizeof(gas_record_t));

    s_u8FlashSlot = slot;
    s_u32LastSaved = s_u32GasMeter;
}

// 从DataFlash读取数据
void ReadFromDataFlash(void)
{
    gas_record_t rec0;
    gas_record_t rec1;
    uint8_t ok0;
    uint8_t ok1;
    uint32_t legacy;

    ok0 = read_gas_record(GAS_FLASH_SLOT0, &rec0);
    ok1 = read_gas_record(GAS_FLASH_SLOT1, &rec1);

    if(ok0 && ok1)
    {
        if(rec0.seq >= rec1.seq)
        {
            s_u32GasMeter = rec0.value;
            s_u32FlashSeq = rec0.seq;
            s_u8FlashSlot = 0;
        }
        else
        {
            s_u32GasMeter = rec1.value;
            s_u32FlashSeq = rec1.seq;
            s_u8FlashSlot = 1;
        }
    }
    else if(ok0)
    {
        s_u32GasMeter = rec0.value;
        s_u32FlashSeq = rec0.seq;
        s_u8FlashSlot = 0;
    }
    else if(ok1)
    {
        s_u32GasMeter = rec1.value;
        s_u32FlashSeq = rec1.seq;
        s_u8FlashSlot = 1;
    }
    else
    {
        // 兼容旧固件：旧固件在地址0保存裸uint32
        legacy = 0xFFFFFFFFUL;
        EEPROM_READ(GAS_FLASH_SLOT0, (uint8_t *)&legacy, sizeof(legacy));
        if(legacy < GAS_METER_ROLLOVER)
        {
            s_u32GasMeter = legacy;
            s_u32FlashSeq = 0;
            s_u8FlashSlot = 0;   // 写槽1，保留槽0旧值直到新记录写完
            s_u32LastSaved = s_u32GasMeter;
            SaveToDataFlash();
            return;
        }
        s_u32GasMeter = DEFAULT_GAS_METER;
        s_u32FlashSeq = 0;
        s_u8FlashSlot = 1;
    }

    if(s_u32GasMeter >= GAS_METER_ROLLOVER)
    {
        s_u32GasMeter = 0;
    }
    s_u32LastSaved = s_u32GasMeter;
}

// 电池电压采样
void sample_battery_voltage()
{
    uint16_t u16Adc;
    uint16_t u16Adc2;
    uint16_t u16Adc3;
    uint16_t u16Tmp;

    ADC_InterBATSampInit();
    DelayUs(10);
    (void)ADC_ExcutSingleConver();   // 丢弃首次转换，等内部VBAT通道稳定
    u16Adc  = ADC_ExcutSingleConver();
    u16Adc2 = ADC_ExcutSingleConver();
    u16Adc3 = ADC_ExcutSingleConver();
    ADC_DisablePower();

    // 取3次采样的中值，过滤单次异常低值
    if(u16Adc > u16Adc2)
    {
        u16Tmp = u16Adc;
        u16Adc = u16Adc2;
        u16Adc2 = u16Tmp;
    }
    if(u16Adc2 > u16Adc3)
    {
        u16Tmp = u16Adc2;
        u16Adc2 = u16Adc3;
        u16Adc3 = u16Tmp;
    }
    if(u16Adc > u16Adc2)
    {
        u16Tmp = u16Adc;
        u16Adc = u16Adc2;
        u16Adc2 = u16Tmp;
    }

    if(u16Adc2 <= 2048)
    {
        s_u16BatVolt = ADC_VREF;
    }
    else
    {
        s_u16BatVolt = ((u16Adc2 * ADC_VREF) >> 9) - (3 * ADC_VREF);
    }
}

// TCRT5000采样：发射管关-开-关，差分抵消环境光
void sample_tcrt5000_voltage()
{
    uint16_t vOff1;
    uint16_t vOff2;
    uint16_t vOn;
    uint16_t vAmb;

    // 发射管关，先采环境光
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_20mA);
    GPIOB_ResetBits(GPIO_Pin_15);
    DelayUs(30);

    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
    ADC_ChannelCfg(CH_EXTIN_13);
    (void)ADC_ExcutSingleConver();   // 丢弃首次转换，等ADC稳定
    DelayUs(10);
    vOff1 = ADC_ExcutSingleConver();

    // 发射管开，采反射光
    GPIOB_SetBits(GPIO_Pin_15);
    DelayUs(80);
    vOn = ADC_ExcutSingleConver();

    // 发射管再关，采第二次环境光
    GPIOB_ResetBits(GPIO_Pin_15);
    DelayUs(30);
    vOff2 = ADC_ExcutSingleConver();

    ADC_DisablePower();
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PD);

    // 环境光在开/关采样中相同，差分后抵消
    vAmb = (uint16_t)(((uint32_t)vOff1 + vOff2) >> 1);
    s_u16Tcrt5000 = (vOn > vAmb) ? (vOn - vAmb) : 0;

    // 燃气计量
    if(s_u16FirstPowerOn)
    {
        s_u16FirstPowerOn--;
    }
    else
    {
        if(s_u16Tcrt5000Min > (s_u16Tcrt5000 + TCRT5000_MIN_THRESHOLD))
        {//更新最小值
            s_u16Tcrt5000Min = s_u16Tcrt5000;
            s_bTcrt5000Use = TRUE;
        }

        if(s_u16Tcrt5000 > (s_u16Tcrt5000Min + TCRT5000_THRESHOLD))
        {//检测到反射位置
            if(s_bTcrt5000Use == TRUE)
            {
                s_u32GasMeter += 10;
                if(s_u32GasMeter >= GAS_METER_ROLLOVER)
                {
                    s_u32GasMeter -= GAS_METER_ROLLOVER;
                }
                if(!s_bAdjustActive &&
                   (gas_delta(s_u32GasMeter, s_u32LastSaved) >= GAS_METER_SAVE_STEP))
                {
                    SaveToDataFlash();
                }
                s_u16Tcrt5000Min = s_u16Tcrt5000;
                s_bTcrt5000Use = FALSE;
                s_ucBroadcasterCnt = 5;   // 只有确认到有效脉冲才触发广播
            }
        }
        if(s_u16Tcrt5000Min < 4095)
        {
            s_u16Tcrt5000Min++;
        }

    }
}

// 更新广播数据
void updata_bthome_data()
{
    bthomeData[9] = s_u16BatVolt & 0xFF;
    bthomeData[10] = (s_u16BatVolt >> 8) & 0xFF;
    bthomeData[12] = s_u16Tcrt5000 & 0xFF;
    bthomeData[13] = (s_u16Tcrt5000 >> 8) & 0xFF;
    bthomeData[15] = s_u32GasMeter & 0xFF;
    bthomeData[16] = (s_u32GasMeter >> 8) & 0xFF;
    bthomeData[17] = (s_u32GasMeter >> 16) & 0xFF;
    bthomeData[18] = (s_u32GasMeter >> 24) & 0xFF;
}

// 开启/关闭广播
void gaprole_advert_enable(uint8_t mode)
{
    uint8_t advertisingEnabled = mode ? TRUE : FALSE;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(advertisingEnabled), &advertisingEnabled);
}

// 按键处理：下降沿中断用于捕获短按，300ms任务处理长按
static void process_keys(void)
{
    uint8_t irq;
    uint8_t keyB;
    uint8_t keyP;
    uint8_t keyN;
    uint32_t delta;

    PFIC_DisableIRQ(GPIO_B_IRQn);
    irq = s_u8KeyIrqMask;
    s_u8KeyIrqMask = 0;
    PFIC_EnableIRQ(GPIO_B_IRQn);

    keyB = (GPIOB_ReadPortPin(GPIO_Pin_22) == 0) ? TRUE : FALSE;
    keyP = (GPIOB_ReadPortPin(GPIO_Pin_11) == 0) ? TRUE : FALSE;
    keyN = (GPIOB_ReadPortPin(GPIO_Pin_10) == 0) ? TRUE : FALSE;

    // PB10/PB11 同时为低通常是 USB 主机把 D-/D+ 下拉，不是真的按了 P/N
    if(keyP && keyN)
    {
        keyP = FALSE;
        keyN = FALSE;
        s_bKeyPBlocked = TRUE;
        s_bKeyNBlocked = TRUE;
        irq &= ~(KEY_IRQ_P | KEY_IRQ_N);
    }

    // 上电时已为低的按键，必须释放一次后才响应
    if(s_bKeyPBlocked)
    {
        irq &= ~KEY_IRQ_P;
        keyP = FALSE;
        if(GPIOB_ReadPortPin(GPIO_Pin_11) != 0)
        {
            s_bKeyPBlocked = FALSE;
        }
    }
    if(s_bKeyNBlocked)
    {
        irq &= ~KEY_IRQ_N;
        keyN = FALSE;
        if(GPIOB_ReadPortPin(GPIO_Pin_10) != 0)
        {
            s_bKeyNBlocked = FALSE;
        }
    }

    // B键长按3秒进入OTA模式（10次 * 300ms）
    if(keyB)
    {
        s_u16KeyBDownTime++;
        if(s_u16KeyBDownTime >= 10)
        {
            OtaMode_Request();
            SYS_ResetExecute();
        }
    }
    else
    {
        s_u16KeyBDownTime = 0;
    }

    // B键：按下沿保存一次；若在两次采样之间短按，也保存一次
    if((keyB && !s_bKeyBPressed) || (!keyB && (irq & KEY_IRQ_B)))
    {
        SaveToDataFlash();
    }
    s_bKeyBPressed = keyB;

    // P/N键
    if(keyP || keyN)
    {
        s_bAdjustActive = TRUE;
        s_ucBroadcasterCnt = 5;
        s_u16KeyDownTime++;

        if(s_u16KeyDownTime > 30)
        {//超过9s停止累加，必须松开再按，防止按键卡住时无限增长
            s_u16KeyDownTime = 30;
            delta = 0;
        }
        else if(s_u16KeyDownTime > 20)
        {//6s
            delta = 1000;    //+1立方米
        }
        else if(s_u16KeyDownTime > 10)
        {//3s
            delta = 100;     //+0.1立方米
        }
        else
        {
            delta = 10;      //+0.01立方米
        }

        if(delta)
        {
            if(keyP)
            {
                s_u32GasMeter += delta;
                if(s_u32GasMeter >= GAS_METER_ROLLOVER)
                {
                    s_u32GasMeter -= GAS_METER_ROLLOVER;
                }
            }
            else
            {
                if(s_u32GasMeter > delta)
                {
                    s_u32GasMeter -= delta;
                }
                else
                {
                    s_u32GasMeter = 0;
                }
            }
        }
    }
    else
    {
        s_u16KeyDownTime = 0;
        if(s_bAdjustActive)
        {
            s_bAdjustActive = FALSE;
            SaveToDataFlash();   // 长按松手保存一次
        }
        else if(irq & (KEY_IRQ_P | KEY_IRQ_N))
        {
            // 两次采样之间的短按：+/-0.01立方米
            if(irq & KEY_IRQ_P)
            {
                s_u32GasMeter += 10;
                if(s_u32GasMeter >= GAS_METER_ROLLOVER)
                {
                    s_u32GasMeter -= GAS_METER_ROLLOVER;
                }
            }
            if(irq & KEY_IRQ_N)
            {
                if(s_u32GasMeter > 10)
                {
                    s_u32GasMeter -= 10;
                }
                else
                {
                    s_u32GasMeter = 0;
                }
            }
            s_ucBroadcasterCnt = 5;
            SaveToDataFlash();
        }
    }
}

typedef struct
{
    uint8_t magic;
    uint8_t reserved[3];
} ota_mode_record_t;

uint8_t OtaMode_IsRequested(void)
{
    ota_mode_record_t rec;
    EEPROM_READ(OTA_MODE_FLASH_ADDR, (uint8_t *)&rec, sizeof(rec));
    return (rec.magic == OTA_MODE_MAGIC) ? TRUE : FALSE;
}

void OtaMode_ClearRequest(void)
{
    EEPROM_ERASE(OTA_MODE_FLASH_ADDR, EEPROM_BLOCK_SIZE);
}

void OtaMode_Request(void)
{
    ota_mode_record_t rec;
    rec.magic = OTA_MODE_MAGIC;
    rec.reserved[0] = 0;
    rec.reserved[1] = 0;
    rec.reserved[2] = 0;
    EEPROM_ERASE(OTA_MODE_FLASH_ADDR, EEPROM_BLOCK_SIZE);
    EEPROM_WRITE(OTA_MODE_FLASH_ADDR, (uint8_t *)&rec, sizeof(rec));
}

void Broadcaster_Init(void)
{
#ifdef GAS_RECOVERY_VALUE
    // 一次性恢复固件：擦除双槽并写入指定燃气值，然后停机等待重新烧录
    EEPROM_ERASE(GAS_FLASH_SLOT0, EEPROM_BLOCK_SIZE);
    EEPROM_ERASE(GAS_FLASH_SLOT1, EEPROM_BLOCK_SIZE);
    s_u32GasMeter = (uint32_t)GAS_RECOVERY_VALUE;
    s_u32FlashSeq = 0;
    s_u8FlashSlot = 1;
    SaveToDataFlash();
    while(1)
    {
        __WFI();
    }
#endif

    Broadcaster_TaskID = TMOS_ProcessEventRegister(Broadcaster_ProcessEvent);

    // BTHome v2 文本对象 0x53：固件版本号
    bthomeData[3] = (uint8_t)(15 + 2 + FW_VERSION_LEN);
    bthomeData[19] = 0x53;
    bthomeData[20] = (uint8_t)FW_VERSION_LEN;
    tmos_memcpy(&bthomeData[21], FW_VERSION_STRING, FW_VERSION_LEN);

    uint8_t adv_type = GAP_ADTYPE_ADV_NONCONN_IND;
    GAPRole_SetParameter(GAPROLE_ADV_EVENT_TYPE, 1, &adv_type);

    uint16_t adv_int = SBP_ADVERT_INTERVAL;
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, adv_int);
    GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, adv_int);

    gaprole_advert_enable(FALSE);

    // 上电先关闭红外发射管，避免GPIO全上拉导致其导通
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PD);

    // 按键上拉输入
    GPIOB_ModeCfg(GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_22, GPIO_ModeIN_PU);

    // 上电时就为低的按键先屏蔽，必须释放一次后才响应
    // PB10/PB11 也是 USB D-/D+，接 USB 时会被主机下拉，不能当成按键
    if(GPIOB_ReadPortPin(GPIO_Pin_11) == 0)
    {
        s_bKeyPBlocked = TRUE;
    }
    if(GPIOB_ReadPortPin(GPIO_Pin_10) == 0)
    {
        s_bKeyNBlocked = TRUE;
    }

    // 按键下降沿中断，用于捕获短按和唤醒
    GPIOB_ITModeCfg(GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_22, GPIO_ITMode_FallEdge);
    GPIOB_ClearITFlagBit(GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_22);
    R16_PB_INT_EN |= KEY_PB_IRQ_MASK;
    PFIC_EnableIRQ(GPIO_B_IRQn);

    // 允许GPIO下降沿唤醒
    sys_safe_access_enable();
    R8_SLP_WAKE_CTRL |= RB_SLP_GPIO_WAKE | RB_GPIO_WAKE_MODE | RB_WAKE_EV_MODE;
    sys_safe_access_disable();

    // 启动设备事件
    tmos_set_event(Broadcaster_TaskID, SBP_START_DEVICE_EVT);

    // 300ms燃气检测事件
    tmos_start_task(Broadcaster_TaskID, SBP_GAS_METER_EVT, SBP_GAS_METER_EVT_PERIOD);

    // 1分钟广播使能事件
    tmos_start_task(Broadcaster_TaskID, SBP_BROADCASTER_EVT, SBP_BROADCASTER_EVT_PERIOD);
}

uint16_t Broadcaster_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg = tmos_msg_receive(Broadcaster_TaskID);
        if(pMsg) tmos_msg_deallocate(pMsg);
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        bStatus_t status = GAPRole_BroadcasterStartDevice(&Broadcaster_BroadcasterCBs);
        (void)status;
        // 读取历史燃气数据
        ReadFromDataFlash();
        // 配置ADC引脚
        GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeIN_Floating);
        // 采样电池电压
        sample_battery_voltage();
        // 上电后立即广播一次
        s_ucBroadcasterCnt = 1;
        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_GAS_METER_EVT)
    {
        tmos_start_task(Broadcaster_TaskID, SBP_GAS_METER_EVT, SBP_GAS_METER_EVT_PERIOD);

        process_keys();
        sample_tcrt5000_voltage();

        // 需要广播
        if(s_ucBroadcasterCnt)
        {
            s_ucBroadcasterCnt--;
            tmos_start_task(Broadcaster_TaskID, SBP_ADVERT_DISABLE_EVT, SBP_ADVERT_DISABLE_EVT_PERIOD * SBP_ADVERT_DISABLE_EVT_TIME);
            updata_bthome_data();     // 更新BTHOME数据
            GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(bthomeData), bthomeData);
            gaprole_advert_enable(TRUE);   // 开启广播
        }

        return (events ^ SBP_GAS_METER_EVT);
    }

    if(events & SBP_BROADCASTER_EVT)
    {
        tmos_start_task(Broadcaster_TaskID, SBP_BROADCASTER_EVT, SBP_BROADCASTER_EVT_PERIOD);
        sample_battery_voltage();
        s_ucBroadcasterCnt = 1;
        return (events ^ SBP_BROADCASTER_EVT);
    }

    if(events & SBP_ADVERT_DISABLE_EVT)
    {
        gaprole_advert_enable(FALSE);
        return (events ^ SBP_ADVERT_DISABLE_EVT);
    }

    return 0;
}

static void Broadcaster_StateNotificationCB(gapRole_States_t newState)
{
    switch(newState)
    {
        default: break;
    }
}
