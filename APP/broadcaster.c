#include "CONFIG.h"
#include "devinfoservice.h"
#include "broadcaster.h"
#include "peripheral.h"
#include "gas_log_policy.h"
#include "scheduled_ota.h"
#include "gas_counter_policy.h"
#include "threshold_policy.h"
#include "threshold_key_policy.h"
#include "bthome_threshold.h"

/* 按键中断标志 */
#define KEY_IRQ_B                 0x01
#define KEY_IRQ_P                 0x02
#define KEY_IRQ_N                 0x04
#define KEY_PB_IRQ_MASK           (GPIO_Pin_10 | GPIO_Pin_11 | (GPIO_Pin_22 >> 14))

/* DataFlash 双备份记录，两个槽位相隔4KB，避免一次擦除影响另一个槽位 */
#define GAS_FLASH_MAGIC           0x47415331UL  /* "GAS1" */
#define GAS_FLASH_SLOT0           0
#define GAS_FLASH_SLOT1           4096
#define GAS_ADJUST_MAX_TOTAL      2000000UL /* max change per press: 2000 m3 */

/* Gas reading is an append-only log inside one 4 KB block:
   256 records x 16 bytes. A block is erased only when it is full,
   i.e. once per 256 saves instead of once per save. */
#define GAS_LOG_SIZE              EEPROM_BLOCK_SIZE
#define GAS_LOG_REC_SIZE          16
#define GAS_LOG_FULL              GAS_LOG_SIZE

/* TCRT5000 pulse threshold, adjustable over the OTA channel (fw >= 1.4.1) */
#define TCRT_CONFIG_MAGIC         0x54435231UL  /* "TCR1" */
#define TCRT_CONFIG_ADDR          0x3000        /* DataFlash block 3 */

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint32_t value;
    uint32_t crc;          /* 低16位为CRC16 */
} gas_record_t;

typedef struct
{
    uint32_t magic;
    uint16_t threshold;
    uint16_t reserved;
    uint32_t crc;          /* low 16 bits = CRC16 over the first 8 bytes */
} tcrt_config_t;

static uint8_t Broadcaster_TaskID;

static uint32_t s_u32ScheduledOtaMinutes = 0;
static uint8_t s_ucBroadcasterCnt = 0;    // 广播次数
static uint16_t s_u16BatVolt = 0;         // 电池采样电压
static uint32_t s_u32GasMeter = 0;        // 燃气表数值，0.001立方米/bit
static uint16_t s_u16FirstPowerOn = 200;  // 上电1分钟内不计量燃气
static uint8_t s_bTcrt5000Use = FALSE;    // 是否已建立反射基准
static uint16_t s_u16Tcrt5000Min = 4095;  // 反射差分最小值
static uint16_t s_u16Tcrt5000 = 0;
static uint16_t s_u16TcrtThreshold = TCRT5000_THRESHOLD_DEFAULT; /* runtime pulse threshold */
static uint8_t s_bKeyPBlocked = FALSE;    // P键上电已拉低，释放前忽略
static uint8_t s_bKeyNBlocked = FALSE;    // N键上电已拉低，释放前忽略

static uint16_t s_u16KeyBDownTime = 0;    // B键长按计时（OTA模式）

static uint8_t s_bKeyBPressed = FALSE;    // B键上一次采样状态
static ThresholdKeyState s_thresholdKey;   // P/N键正在调整
static volatile uint8_t s_u8KeyIrqMask = 0; // 按键下降沿中断标志

static uint32_t s_u32FlashSeq = 0;
static uint8_t s_bFlashSeqDirty = FALSE;        /* last accepted sequence */
static uint8_t s_u8FlashBlock = 0;        /* active 4 KB log block */
static uint16_t s_u16FlashOffset = 0;     /* next append offset */
static uint16_t s_u16SaveRetryDelay = 0;  /* ticks to wait after a failed save */
static uint8_t s_bGasStoreLoaded = FALSE; /* log state scanned at least once */
static uint32_t s_u32LastSaved = 0;       /* value of the last accepted record */

#define FW_VERSION_LEN (sizeof(FW_VERSION_STRING) - 1)
static uint8_t bthomeData[BthomeAdvertSize(FW_VERSION_LEN)] =
{
    0x02, 0x01, 0x06,             // Flags data
    0x0F, 0x16, 0xD2, 0xFC, 0x40, // 服务数据bthomev2
    0x0C, 0x00, 0x00,             // 电池电压，0.001V/bit
    0x3D, 0x00, 0x00,             // ADC反射差分，1CNT/bit
    0x4C, 0x00, 0x00, 0x00, 0x00, // 燃气表读数，0.001立方米/bit
    0x3E, 0x00, 0x00, 0x00, 0x00  // threshold feedback (BTHome count32)
};

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
    return GasCounterDelta(now, saved);
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
    return TRUE;
}

/* ------------------------------------------------------------------
   Gas reading store: append-only log
   ------------------------------------------------------------------
   Two 4 KB blocks are used, each split into 256 records of 16 bytes.
   A save appends one record to the active block; nothing is erased until
   the block is full. Then the *other* block (the previous generation) is
   erased and becomes the active one, so a power loss can only ever lose
   the single record that was being appended, never the whole log.
   A record is accepted only after being read back and verified.
   ------------------------------------------------------------------ */

static uint32_t gas_block_addr(uint8_t block)
{
    return (block == 0) ? GAS_FLASH_SLOT0 : GAS_FLASH_SLOT1;
}

static int gas_log_read_sequence(uint8_t block, uint16_t index, uint32_t *sequence, void *context)
{
    gas_record_t rec;
    (void)context;
    if(!read_gas_record(gas_block_addr(block) + ((uint32_t)index * GAS_LOG_REC_SIZE), &rec))
    {
        return 0;
    }
    *sequence = rec.seq;
    return 1;
}

static void gas_store_recover_sequence(void)
{
    if(!s_bFlashSeqDirty)
    {
        return;
    }
    s_u32FlashSeq = GasLogRecoverMaxSequence(s_u32FlashSeq, gas_log_read_sequence, 0);
    s_bFlashSeqDirty = FALSE;
}

static uint8_t gas_record_blank(uint32_t addr)
{
    uint8_t buf[GAS_LOG_REC_SIZE] __attribute__((aligned(4)));
    uint8_t i;

    EEPROM_READ(addr, buf, GAS_LOG_REC_SIZE);
    for(i = 0; i < GAS_LOG_REC_SIZE; i++)
    {
        if(buf[i] != 0xFF)
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* first blank record at or after off; GAS_LOG_FULL when the block is full */
static uint16_t gas_next_blank(uint8_t block, uint16_t off)
{
    uint32_t base = gas_block_addr(block);

    while((uint32_t)off + GAS_LOG_REC_SIZE <= GAS_LOG_SIZE)
    {
        if(gas_record_blank(base + off))
        {
            return off;
        }
        off += GAS_LOG_REC_SIZE;
    }
    return GAS_LOG_FULL;
}

/* scan both blocks, keep the record with the highest seq */
static uint8_t gas_store_load(void)
{
    gas_record_t rec;
    uint32_t seq = 0;
    uint8_t have = FALSE;
    uint8_t best_block = 0;
    uint16_t best_off = 0;
    uint8_t block;
    uint16_t off;

    for(block = 0; block < 2; block++)
    {
        uint32_t base = gas_block_addr(block);

        for(off = 0; (uint32_t)off + GAS_LOG_REC_SIZE <= GAS_LOG_SIZE; off += GAS_LOG_REC_SIZE)
        {
            if(!read_gas_record(base + off, &rec))
            {
                continue;
            }
            if(!have || (rec.seq >= seq))
            {
                have = TRUE;
                seq = rec.seq;
                best_block = block;
                best_off = off;
                s_u32GasMeter = rec.value;
            }
        }
    }

    if(have)
    {
        s_u32FlashSeq = seq;
        s_u8FlashBlock = best_block;
        s_u16FlashOffset = gas_next_blank(best_block, best_off + GAS_LOG_REC_SIZE);
        s_u32LastSaved = s_u32GasMeter;
    }
    return have;
}

/* move the log to the other block; the active log is never erased */
static uint8_t gas_store_switch(void)
{
    uint8_t spare = s_u8FlashBlock ^ 1;
    uint32_t addr = gas_block_addr(spare);

    if(EEPROM_ERASE(addr, EEPROM_BLOCK_SIZE) != 0)
    {
        return FALSE;
    }
    s_u8FlashBlock = spare;
    s_u16FlashOffset = 0;
    return TRUE;
}

/* append one record, verify by read-back; 0 = ok, 0xFF = failed */
static uint8_t gas_store_append(uint32_t value)
{
    gas_record_t rec;
    gas_record_t check;
    uint32_t addr;
    uint16_t off;
    uint8_t attempt;

    for(attempt = 0; attempt < 3; attempt++)
    {
        gas_store_recover_sequence();
        off = gas_next_blank(s_u8FlashBlock, s_u16FlashOffset);
        if(off >= GAS_LOG_FULL)
        {
            if(!gas_store_switch())
            {
                return 0xFF;
            }
            off = 0;
        }

        rec.magic = GAS_FLASH_MAGIC;
        rec.seq = s_u32FlashSeq + 1;
        rec.value = value;
        rec.crc = (uint32_t)crc16_ccitt((const uint8_t *)&rec, 12);

        addr = gas_block_addr(s_u8FlashBlock) + off;
        if(EEPROM_WRITE(addr, (uint8_t *)&rec, GAS_LOG_REC_SIZE) != 0)
        {
            s_bFlashSeqDirty = TRUE;
            s_u16FlashOffset = off + GAS_LOG_REC_SIZE;
            continue;
        }
        if(read_gas_record(addr, &check) &&
           (check.seq == rec.seq) && (check.value == value))
        {
            s_u32FlashSeq = rec.seq;
            s_u16FlashOffset = off + GAS_LOG_REC_SIZE;
            s_u32LastSaved = value;
            return 0;
        }
        s_bFlashSeqDirty = TRUE;
        s_u16FlashOffset = off + GAS_LOG_REC_SIZE;
    }
    return 0xFF;
}

static void gas_save_internal(uint8_t force)
{
    if(!force && s_u16SaveRetryDelay)
    {
        return;
    }
    if(gas_store_append(s_u32GasMeter) == 0)
    {
        s_u16SaveRetryDelay = 0;
    }
    else
    {
        /* retry in ~15 s instead of hitting the flash every 300 ms */
        s_u16SaveRetryDelay = 50;
    }
}

void SaveToDataFlash(void)
{
    gas_save_internal(FALSE);
}

/* user-initiated save (key press): ignore the retry backoff */
void SaveToDataFlashNow(void)
{
    gas_save_internal(TRUE);
}

void ReadFromDataFlash(void)
{
    uint32_t legacy;

    s_u32GasMeter = DEFAULT_GAS_METER;
    s_u32FlashSeq = 0;
    s_bFlashSeqDirty = FALSE;
    s_u8FlashBlock = 0;
    s_u16FlashOffset = gas_next_blank(0, 0);
    s_u32LastSaved = s_u32GasMeter;

    if(gas_store_load())
    {
        s_u32LastSaved = s_u32GasMeter;
        s_bGasStoreLoaded = TRUE;
        return;
    }

    /* data from a firmware without the log layout: bare uint32 at block 0 */
    legacy = 0xFFFFFFFFUL;
    EEPROM_READ(GAS_FLASH_SLOT0, (uint8_t *)&legacy, 4);
    if(legacy != 0xFFFFFFFFUL)
    {
        s_u32GasMeter = legacy;
        s_u32LastSaved = legacy;
        s_u16FlashOffset = gas_next_blank(0, 0);
        gas_store_append(legacy);
    }
    s_bGasStoreLoaded = TRUE;
}

static uint8_t read_tcrt_config(tcrt_config_t *cfg)
{
    EEPROM_READ(TCRT_CONFIG_ADDR, (uint8_t *)cfg, sizeof(tcrt_config_t));

    if(cfg->magic != TCRT_CONFIG_MAGIC)
    {
        return FALSE;
    }
    if(cfg->crc != (uint32_t)crc16_ccitt((const uint8_t *)cfg, 8))
    {
        return FALSE;
    }
    if((cfg->threshold < TCRT5000_THRESHOLD_MIN) ||
       (cfg->threshold > TCRT5000_THRESHOLD_MAX))
    {
        return FALSE;
    }
    return TRUE;
}

/* Load the stored pulse threshold (DataFlash block 3), fall back to default */
void ReadConfigFromDataFlash(void)
{
    tcrt_config_t cfg;

    if(read_tcrt_config(&cfg))
    {
        s_u16TcrtThreshold = cfg.threshold;
    }
    else
    {
        s_u16TcrtThreshold = TCRT5000_THRESHOLD_DEFAULT;
    }
}

uint16_t Broadcaster_GetThreshold(void)
{
    return s_u16TcrtThreshold;
}

/* Persist a new pulse threshold and apply it immediately.
   Returns 0 on success, 0xFE on out-of-range, 0xFF on verify failure. */
uint8_t Broadcaster_SetThreshold(uint16_t value)
{
    tcrt_config_t cfg;
    tcrt_config_t check;

    if((value < TCRT5000_THRESHOLD_MIN) || (value > TCRT5000_THRESHOLD_MAX))
    {
        return 0xFE;
    }

    cfg.magic = TCRT_CONFIG_MAGIC;
    cfg.threshold = value;
    cfg.reserved = 0;
    cfg.crc = (uint32_t)crc16_ccitt((const uint8_t *)&cfg, 8);

    EEPROM_ERASE(TCRT_CONFIG_ADDR, EEPROM_BLOCK_SIZE);
    EEPROM_WRITE(TCRT_CONFIG_ADDR, (uint8_t *)&cfg, sizeof(tcrt_config_t));

    if(!read_tcrt_config(&check) || (check.threshold != value))
    {
        return 0xFF;
    }
    s_u16TcrtThreshold = value;
    return 0;
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

/* battery voltage in mV, for the OTA mode INFO response */
uint16_t Broadcaster_GetBatteryMv(void)
{
    return s_u16BatVolt;
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

        if(s_u16Tcrt5000 > (s_u16Tcrt5000Min + s_u16TcrtThreshold))
        {//检测到反射位置
            if(s_bTcrt5000Use == TRUE)
            {
                s_u32GasMeter += 10;
                if(gas_delta(s_u32GasMeter, s_u32LastSaved) >= GAS_METER_SAVE_STEP)
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

    /* the save backoff counts 300 ms samples, not save attempts */
    if(s_u16SaveRetryDelay)
    {
        s_u16SaveRetryDelay--;
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
    BthomeWriteThreshold(bthomeData, s_u16TcrtThreshold);
}

// 开启/关闭广播
void gaprole_advert_enable(uint8_t mode)
{
    uint8_t advertisingEnabled = mode ? TRUE : FALSE;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(advertisingEnabled), &advertisingEnabled);
}

// 按键处理：下降沿中断用于捕获短按，300ms任务处理长按
static void adjust_threshold(int32_t delta)
{
    int32_t value = (int32_t)s_u16TcrtThreshold + delta;
    if(value < 0) value = 0;
    s_u16TcrtThreshold = ThresholdClamp((uint32_t)value, TCRT5000_THRESHOLD_MIN, TCRT5000_THRESHOLD_MAX);
    s_ucBroadcasterCnt = 1;  // immediate P/N feedback on the next 300 ms sample
}

static void save_threshold_now(void)
{
    if(Broadcaster_SetThreshold(s_u16TcrtThreshold) != 0)
    {
        ReadConfigFromDataFlash();
    }
    s_ucBroadcasterCnt = 1;  // advertise the final persisted/reverted value
}

static void process_keys(void)
{
    uint8_t irq;
    uint8_t keyB;
    uint8_t keyP;
    uint8_t keyN;
    uint16_t step;
    int16_t short_delta;

    PFIC_DisableIRQ(GPIO_B_IRQn);
    irq = s_u8KeyIrqMask;
    s_u8KeyIrqMask = 0;
    PFIC_EnableIRQ(GPIO_B_IRQn);

    keyB = (GPIOB_ReadPortPin(GPIO_Pin_22) == 0) ? TRUE : FALSE;
    keyP = (GPIOB_ReadPortPin(GPIO_Pin_11) == 0) ? TRUE : FALSE;
    keyN = (GPIOB_ReadPortPin(GPIO_Pin_10) == 0) ? TRUE : FALSE;

    /* P+N low is the existing USB state, never a threshold adjustment. */
    if(keyP && keyN)
    {
        keyP = FALSE;
        keyN = FALSE;
        s_bKeyPBlocked = TRUE;
        s_bKeyNBlocked = TRUE;
        irq &= ~(KEY_IRQ_P | KEY_IRQ_N);
    }

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

    /* B remains the manual OTA entry and also flushes the raw counter log. */
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
    if((keyB && !s_bKeyBPressed) || (!keyB && (irq & KEY_IRQ_B)))
    {
        SaveToDataFlashNow();
    }
    s_bKeyBPressed = keyB;

    if(keyP || keyN)
    {
        ThresholdKeyStart(&s_thresholdKey, keyP ? THRESHOLD_KEY_P : THRESHOLD_KEY_N);
        step = ThresholdKeyHold(&s_thresholdKey);
        if(step)
        {
            adjust_threshold(keyP ? (int32_t)step : -(int32_t)step);
        }
    }
    else
    {
        if(s_thresholdKey.active)
        {
            short_delta = ThresholdKeyRelease(&s_thresholdKey);
            if(short_delta)
            {
                adjust_threshold(short_delta);
            }
            save_threshold_now();
        }
        else if(irq & (KEY_IRQ_P | KEY_IRQ_N))
        {
            if(irq & KEY_IRQ_P) adjust_threshold(1);
            if(irq & KEY_IRQ_N) adjust_threshold(-1);
            save_threshold_now();
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

uint8_t OtaMode_IsScheduledRequest(void)
{
    ota_mode_record_t rec;
    EEPROM_READ(OTA_MODE_FLASH_ADDR, (uint8_t *)&rec, sizeof(rec));
    return (rec.magic == OTA_MODE_MAGIC) &&
           OtaRequestReasonIsScheduled(rec.reserved[0]);
}

static void OtaMode_WriteRequest(uint8_t reason)
{
    ota_mode_record_t rec;
    rec.magic = OTA_MODE_MAGIC;
    rec.reserved[0] = reason;
    rec.reserved[1] = 0;
    rec.reserved[2] = 0;
    EEPROM_ERASE(OTA_MODE_FLASH_ADDR, EEPROM_BLOCK_SIZE);
    EEPROM_WRITE(OTA_MODE_FLASH_ADDR, (uint8_t *)&rec, sizeof(rec));
}

void OtaMode_Request(void)
{
    OtaMode_WriteRequest(OTA_REQUEST_MANUAL);
}

void OtaMode_RequestScheduled(void)
{
    OtaMode_WriteRequest(OTA_REQUEST_SCHEDULED);
}

void Broadcaster_Init(void)
{
#ifdef GAS_RECOVERY_VALUE
    // 一次性恢复固件：擦除双槽并写入指定燃气值，然后停机等待重新烧录
    EEPROM_ERASE(GAS_FLASH_SLOT0, EEPROM_BLOCK_SIZE);
    EEPROM_ERASE(GAS_FLASH_SLOT1, EEPROM_BLOCK_SIZE);
    s_u32GasMeter = (uint32_t)GAS_RECOVERY_VALUE;
    s_u32FlashSeq = 0;
    s_u8FlashBlock = 0;
    s_u16FlashOffset = 0;
    SaveToDataFlashNow();
    while(1)
    {
        __WFI();
    }
#endif

    ReadConfigFromDataFlash();

    Broadcaster_TaskID = TMOS_ProcessEventRegister(Broadcaster_ProcessEvent);

    // BTHome v2 文本对象 0x53：固件版本号
    bthomeData[3] = BthomeAdvertDataLength(FW_VERSION_LEN);
    bthomeData[BTHOME_VERSION_OFFSET] = 0x53;
    bthomeData[BTHOME_VERSION_OFFSET + 1] = (uint8_t)FW_VERSION_LEN;
    tmos_memcpy(&bthomeData[BTHOME_VERSION_OFFSET + 2], FW_VERSION_STRING, FW_VERSION_LEN);

    uint8_t adv_type = GAP_ADTYPE_ADV_IND;
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
        /* Peripheral_InitNormal owns the GAP Role in normal mode. */
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
        if(s_ucBroadcasterCnt && !Peripheral_NormalLinkActive())
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
        s_u32ScheduledOtaMinutes++;
        if(ScheduledOta_ShouldRequest(s_u32ScheduledOtaMinutes))
        {
            s_u32ScheduledOtaMinutes = 0;
            OtaMode_RequestScheduled();
            SYS_ResetExecute();
        }
        sample_battery_voltage();
        s_ucBroadcasterCnt = 1;
        return (events ^ SBP_BROADCASTER_EVT);
    }

    if(events & SBP_ADVERT_DISABLE_EVT)
    {
        if(!Peripheral_NormalLinkActive())
        {
            gaprole_advert_enable(FALSE);
        }
        return (events ^ SBP_ADVERT_DISABLE_EVT);
    }

    return 0;
}
