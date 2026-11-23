/********************************** (C) COPYRIGHT *******************************
 * File Name          : Peripheral.C
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/10
 * Description        : 外设从机应用程序，初始化广播连接参数，然后广播，直至连接主机后，通过自定义服务传输数据
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "GATTprofile.h"
#include "Peripheral.h"
#include "OTA.h"
#include "ota_validation.h"
#include "ota_crc32.h"
#include "remote_ota_policy.h"
#include "scheduled_ota.h"
#include "OTAprofile.h"

/* defined in broadcaster.c */
extern void sample_battery_voltage(void);
extern uint16_t Broadcaster_GetBatteryMv(void);
extern uint16_t Broadcaster_GetThreshold(void);
extern uint8_t Broadcaster_SetThreshold(uint16_t value);
extern void ReadConfigFromDataFlash(void);
extern void ReadFromDataFlash(void);
extern void OtaMode_Request(void);

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// How often to perform periodic event
#define SBP_PERIODIC_EVT_PERIOD              1000

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL         160

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely

#define DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL    6

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    12

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY        0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT         1000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST        TRUE

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL        6

// Company Identifier: WCH
#define WCH_COMPANY_ID                       0x07D7

#define INVALID_CONNHANDLE                   0xFFFF

// Length of bd addr as a string
#define B_ADDR_STR_LEN                       15

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t Peripheral_TaskID = 0xff; // Task ID for internal task/event processing
static uint8_t  s_bOtaConnected = FALSE;
static uint16_t s_u16OtaIdleTicks = 0;
static uint16_t s_u16OtaTotalTicks = 0;
static uint16_t s_u16OtaIdleLimit = OTA_MANUAL_IDLE_TICKS;
static uint8_t s_bNormalMode = FALSE;
static RemoteOtaControlState s_remoteOta;

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scanRspData[31] = {
    // complete name
    0x0D, // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'G', 'a', 's', 'M', 'e', 't', 'e', 'r', ' ', 'O', 'T', 'A',
    // connection interval range
    0x05, // length of this data
    GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), // 100ms
    HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
    LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), // 1s
    HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),

    // Tx power level
    0x02, // length of this data
    GAP_ADTYPE_POWER_LEVEL,
    0 // 0dBm
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8_t advertData[] = {
    // Flags; this sets the device to use limited discoverable
    // mode (advertises for 30 seconds at a time) instead of general
    // discoverable mode (advertises indefinitely)
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    // service UUID, to notify central devices what services are included
    // in this peripheral
    0x05,                  // length of this data
    GAP_ADTYPE_16BIT_MORE, // some of the UUID's, but not all
    LO_UINT16(SIMPLEPROFILE_SERV_UUID),
    HI_UINT16(SIMPLEPROFILE_SERV_UUID),
    LO_UINT16(OTAPROFILE_SERV_UUID),
    HI_UINT16(OTAPROFILE_SERV_UUID)
};

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "GasMeter OTA";
static uint8_t attNormalDeviceName[GAP_DEVICE_NAME_LEN] = "GasMeter";

// OTA IAP VARIABLES
/* OTA通讯的帧 */
OTA_IAP_CMD_t iap_rec_data;

/* OTA解析结果 */
uint32_t OpParaDataLen = 0;
uint32_t OpAdd = 0;

/* flash的数据临时存储 */
__attribute__((aligned(8))) uint8_t block_buf[16];

/* Image跳转函数地址定义 */
typedef int (*pImageTaskFn)(void);
pImageTaskFn user_image_tasks;

/* Flash 擦除过程 */
uint32_t EraseAdd = 0;      //擦除地址
uint32_t EraseBlockNum = 0; //需要擦除的块数
uint32_t EraseBlockCnt = 0; //擦除的块计数

/* FLASH 校验过程 */
uint8_t VerifyStatus = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void performPeriodicTask(void);
static void simpleProfileChangeCB(uint8_t paramID);
void        OTA_IAPReadDataComplete(unsigned char index);
void        OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len);
void        Rec_OTA_IAP_DataDeal(void);
void        OTA_IAP_SendCMDDealSta(uint8_t deal_status);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Peripheral_PeripheralCBs = {
    peripheralStateNotificationCB, // Profile State Change Callbacks
    NULL,                          // When a valid RSSI is read from controller (not used by application)
    NULL};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
    NULL, // Passcode callback (not used by application)
    NULL  // Pairing / Bonding state Callback (not used by application)
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t Peripheral_SimpleProfileCBs = {
    simpleProfileChangeCB // Charactersitic value change callback
};

// Simple GATT Profile Callbacks
static OTAProfileCBs_t Peripheral_OTA_IAPProfileCBs = {
    OTA_IAPReadDataComplete, // Charactersitic value change callback
    OTA_IAPWriteData
};

// Callback when the connection parameteres are updated.
void PeripheralParamUpdate(uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout);

gapRolesParamUpdateCB_t PeripheralParamUpdate_t = NULL;

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Peripheral_Init
 *
 * @brief   Initialization function for the Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Peripheral_Init()
{
    s_bNormalMode = FALSE;
    /* OTA mode never runs Broadcaster_Init, so load the stored  */
    /* pulse threshold explicitly for the INFO response.        */
    ReadConfigFromDataFlash();
    ReadFromDataFlash();

    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    // Setup the GAP Peripheral Role Profile
    {
        // For other hardware platforms, device starts advertising upon initialization
        uint8_t initial_advertising_enable = TRUE;

        // Set the GAP Role Parameters
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);

        //PeripheralParamUpdate_t = (gapRolesParamUpdateCB_t *)PeripheralParamUpdate;

        //PRINT( "PeripheralParamUpdate_t %08x \n",(int)PeripheralParamUpdate_t );

        //GAPRole_PeripheralRegisterAppCBs( (gapRolesParamUpdateCB_t *)&PeripheralParamUpdate );
    }

    // Set advertising interval
    {
        uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;

        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
    }

    // Setup the GAP Bond Manager
    {
        uint32_t passkey = 0; // passkey "000000"
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = TRUE;
        uint8_t  ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
        uint8_t  bonding = TRUE;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);           // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
    DevInfo_AddService();                        // Device Information Service
    SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile
    OTAProfile_AddService(GATT_ALL_SERVICES);

    // Set the GAP Characteristics
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

    // Setup the SimpleProfile Characteristic Values
    {
        uint8_t charValue1 = 1;
        uint8_t charValue2 = 2;
        uint8_t charValue3 = 3;
        uint8_t charValue4[SIMPLEPROFILE_CHAR4_LEN] = "01234567";
        uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = {1, 2, 3, 4, 5};

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, sizeof(uint8_t), &charValue1);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, sizeof(uint8_t), &charValue2);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, sizeof(uint8_t), &charValue3);

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, SIMPLEPROFILE_CHAR4_LEN, charValue4);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN, charValue5);
    }

    //
    {
        //GAPRole_PeripheralRegisterAppCBs( (gapRolesParamUpdateCB_t *)PeripheralParamUpdate_t );
    }

    // Register callback with SimpleGATTprofile
    SimpleProfile_RegisterAppCBs(&Peripheral_SimpleProfileCBs);

    //  Register callback with OTAGATTprofile
    OTAProfile_RegisterAppCBs(&Peripheral_OTA_IAPProfileCBs);

    // Setup a delayed profile startup
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}

void Peripheral_InitNormal(void)
{
    uint8_t initial_advertising_enable = FALSE;
    uint8_t adv_type = GAP_ADTYPE_ADV_IND;

    s_bNormalMode = TRUE;
    RemoteOtaControlInit(&s_remoteOta);
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
    GAPRole_SetParameter(GAPROLE_ADV_EVENT_TYPE, sizeof(uint8_t), &adv_type);
    /* Nothing extra to scan for: the BTHome advertising packet is sufficient. */
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, 0, scanRspData);

    GGS_AddService(GATT_ALL_SERVICES);
    GATTServApp_AddService(GATT_ALL_SERVICES);
    OTAProfile_AddService(GATT_ALL_SERVICES);
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attNormalDeviceName);
    OTAProfile_RegisterAppCBs(&Peripheral_OTA_IAPProfileCBs);

    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}

void PeripheralParamUpdate(uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout)
{
    PRINT("update %d %d %d \n", connInterval, connSlaveLatency, connTimeout);

    //		GAPRole_SendUpdateParam( DEFAULT_DESIRED_MIN_CONN_INTERVAL, DEFAULT_DESIRED_MAX_CONN_INTERVAL,
    //                                 DEFAULT_DESIRED_SLAVE_LATENCY, DEFAULT_DESIRED_CONN_TIMEOUT, GAPROLE_NO_ACTION );
}

/*********************************************************************
 * @fn      Peripheral_ProcessEvent
 *
 * @brief   Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    //  VOID task_id; // TMOS required parameter that isn't used in this function

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        // Start the Device
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        // OTA mode alone owns the idle/total reset timers.
        if(!s_bNormalMode)
        {
            tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        }
        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_PERIODIC_EVT)
    {
        // Restart timer
        if(SBP_PERIODIC_EVT_PERIOD)
        {
            tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        }
        // Perform periodic application task
        performPeriodicTask();
        return (events ^ SBP_PERIODIC_EVT);
    }

    if(events & OTA_REMOTE_RESET_EVT)
    {
        if(s_bNormalMode && s_remoteOta.reset_pending)
        {
            SYS_ResetExecute();
        }
        return (events ^ OTA_REMOTE_RESET_EVT);
    }

    if(events & OTA_REMOTE_TIMEOUT_EVT)
    {
        if(s_bNormalMode && RemoteOtaControlShouldDisconnect(&s_remoteOta) &&
           s_remoteOta.connection_handle != INVALID_CONNHANDLE)
        {
            GAPRole_TerminateLink(s_remoteOta.connection_handle);
        }
        return (events ^ OTA_REMOTE_TIMEOUT_EVT);
    }

    //OTA_FLASH_ERASE_EVT
    if(events & OTA_FLASH_ERASE_EVT)
    {
        uint8_t status;

        PRINT("ERASE:%08x num:%d\r\n", (int)(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE), (int)EraseBlockCnt);
        status = FLASH_ROM_ERASE(EraseAdd + EraseBlockCnt * FLASH_BLOCK_SIZE, FLASH_BLOCK_SIZE);

        /* 擦除失败 */
        if(status != SUCCESS)
        {
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        EraseBlockCnt++;

        /* 擦除结束 */
        if(EraseBlockCnt >= EraseBlockNum)
        {
            PRINT("ERASE Complete\r\n");
            OTA_IAP_SendCMDDealSta(status);
            return (events ^ OTA_FLASH_ERASE_EVT);
        }

        return (events);
    }

    if(events & OTA_REBOOT_EVT)
    {
        DisableAllIRQ();
        SYS_ResetExecute();
        return (events ^ OTA_REBOOT_EVT);
    }

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        default:
            break;
    }
}

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void Peripheral_NormalConnected(gapEstLinkReqEvent_t *event)
{
    if(!s_bNormalMode || event == NULL)
    {
        return;
    }
    RemoteOtaControlSetConnection(&s_remoteOta, event->connectionHandle);
    tmos_stop_task(Peripheral_TaskID, OTA_REMOTE_TIMEOUT_EVT);
    tmos_start_task(Peripheral_TaskID, OTA_REMOTE_TIMEOUT_EVT,
                    REMOTE_OTA_CONTROL_TIMEOUT_TICKS);
}

static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState)
    {
        case GAPROLE_STARTED:
            PRINT("Initialized..\n");
            break;

        case GAPROLE_ADVERTISING:
            PRINT("Advertising..\n");
            break;

        case GAPROLE_CONNECTED:
        {
            s_bOtaConnected = TRUE;
            s_u16OtaIdleTicks = 0;
            gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;
            uint16_t              conn_interval = 0;

            Peripheral_NormalConnected(event);
            conn_interval = event->connInterval;
            PRINT("Connected.. \n");

            if(conn_interval > DEFAULT_DESIRED_MAX_CONN_INTERVAL)
            {
                PRINT("Send Update\r\n");
                GAPRole_PeripheralConnParamUpdateReq(event->connectionHandle,
                                                     DEFAULT_DESIRED_MIN_CONN_INTERVAL,
                                                     DEFAULT_DESIRED_MAX_CONN_INTERVAL,
                                                     DEFAULT_DESIRED_SLAVE_LATENCY,
                                                     DEFAULT_DESIRED_CONN_TIMEOUT,
                                                     Peripheral_TaskID);
            }
            break;
        }
        case GAPROLE_CONNECTED_ADV:
            s_bOtaConnected = TRUE;
            s_u16OtaIdleTicks = 0;
            Peripheral_NormalConnected((gapEstLinkReqEvent_t *)pEvent);
            PRINT("Connected Advertising..\n");
            break;
        case GAPROLE_WAITING:
        {
            s_bOtaConnected = FALSE;
            s_u16OtaIdleTicks = 0;
            uint8_t initial_advertising_enable = TRUE;

            if(s_bNormalMode)
            {
                RemoteOtaControlSetConnected(&s_remoteOta, FALSE);
                tmos_stop_task(Peripheral_TaskID, OTA_REMOTE_TIMEOUT_EVT);
                if(!s_remoteOta.reset_pending)
                {
                    uint8_t disable_advertising = FALSE;
                    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &disable_advertising);
                }
            }
            else
            {
                GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
            }
            PRINT("Disconnected..\n");
        }
        break;

        case GAPROLE_ERROR:
            PRINT("Error..\n");
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the SBP_PERIODIC_EVT
 *          TMOS event. In this example, the value of the third
 *          characteristic in the SimpleGATTProfile service is retrieved
 *          from the profile, and then copied into the value of the
 *          the fourth characteristic.
 *
 * @param   none
 *
 * @return  none
 */
void Peripheral_SetScheduledOta(uint8_t reason)
{
    s_u16OtaIdleLimit = OtaIdleLimitTicks(reason);
}

uint8_t Peripheral_NormalLinkActive(void)
{
    return s_bNormalMode &&
           (s_remoteOta.connected || s_remoteOta.reset_pending);
}

static void performPeriodicTask(void)
{
    // OTA模式超时：无连接5分钟退出；总时长15分钟强制退出
    if(s_bOtaConnected)
    {
        s_u16OtaIdleTicks = 0;
    }
    else
    {
        s_u16OtaIdleTicks++;
        if(s_u16OtaIdleTicks > s_u16OtaIdleLimit)
        {
            SYS_ResetExecute();
        }
    }
    s_u16OtaTotalTicks++;
    if(s_u16OtaTotalTicks > 1440)
    {
        SYS_ResetExecute();
    }
}

/*********************************************************************
 * @fn      simpleProfileChangeCB
 *
 * @brief   Callback from Profile indicating a value change
 *
 * @param   paramID - parameter ID of the value that was changed.
 *
 * @return  none
 */
static void simpleProfileChangeCB(uint8_t paramID)
{
    uint8_t newValue;

    switch(paramID)
    {
        case SIMPLEPROFILE_CHAR1:
            PRINT("profile ChangeCB CHAR1..\n");
            SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR1, &newValue);
            break;

        case SIMPLEPROFILE_CHAR3:
            PRINT("profile ChangeCB CHAR3..\n");
            SimpleProfile_GetParameter(SIMPLEPROFILE_CHAR3, &newValue);
            break;

        default:
            // should not reach here!
            break;
    }
}

/*********************************************************************
 * @fn      OTA_IAP_SendData
 *
 * @brief   OTA IAP发送数据，使用时限制20字节以内
 *
 * @param   p_send_data - 发送数据的指针
 * @param   send_len    - 发送数据的长度
 *
 * @return  none
 */
void OTA_IAP_SendData(uint8_t *p_send_data, uint8_t send_len)
{
    OTAProfile_SendData(OTAPROFILE_CHAR, p_send_data, send_len);
}

/*********************************************************************
 * @fn      OTA_IAP_SendCMDDealSta
 *
 * @brief   OTA IAP执行的状态返回
 *
 * @param   deal_status - 返回的状态
 *
 * @return  none
 */
void OTA_IAP_SendCMDDealSta(uint8_t deal_status)
{
    uint8_t send_buf[2];

    send_buf[0] = deal_status;
    send_buf[1] = 0;
    OTA_IAP_SendData(send_buf, 2);
}

/*********************************************************************
 * @fn      OTA_IAP_CMDErrDeal
 *
 * @brief   OTA IAP异常命令码处理
 *
 * @return  none
 */
void OTA_IAP_CMDErrDeal(void)
{
    OTA_IAP_SendCMDDealSta(0xfe);
}

/*********************************************************************
 * @fn      SwitchImageFlag
 *
 * @brief   切换dataflash里的ImageFlag
 *
 * @param   new_flag    - 切换的ImageFlag
 *
 * @return  none
 */
void SwitchImageFlag(uint8_t new_flag)
{
    uint16_t i;
    uint32_t ver_flag;

    /* 读取第一块 */
    EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);

    /* 擦除第一块 */
    EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);

    /* 更新Image信息 */
    block_buf[0] = new_flag;

    /* 编程DataFlash */
    EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&block_buf[0], 4);
}

/*********************************************************************
 * @fn      DisableAllIRQ
 *
 * @brief   关闭所有的中断
 *
 * @return  none
 */
void DisableAllIRQ(void)
{
    SYS_DisableAllIrq(NULL);
}

/*********************************************************************
 * @fn      Rec_OTA_IAP_DataDeal
 *
 * @brief   接收到OTA数据包处理
 *
 * @return  none
 */
/* Fast zlib-compatible CRC32 over Image B. The table-driven helper avoids
   blocking the BLE task with the old eight-iteration-per-byte loop. */
static uint32_t OTA_Crc32Image(void)
{
    return OtaCrc32((const uint8_t *)IMAGE_B_START_ADD, (uint32_t)IMAGE_SIZE);
}

void Rec_OTA_IAP_DataDeal(void)
{
    switch(iap_rec_data.other.buf[0])
    {
        /* 编程 */
        case CMD_IAP_PROM:
        {
            uint32_t i;
            uint8_t  status;

            OpParaDataLen = iap_rec_data.program.len;
            OpAdd = (uint32_t)(iap_rec_data.program.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.program.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;

            PRINT("IAP_PROM: %08x len:%d \r\n", (int)OpAdd, (int)OpParaDataLen);

            /* 当前是ImageA，直接编程 */
            /* split at 256-byte page boundaries: one ROM write must never
               cross a flash page, otherwise a large chunk corrupts data */
            {
                uint8_t *pData = iap_rec_data.program.buf;
                uint16_t u16Left = (uint16_t)OpParaDataLen;
                uint32_t u32Addr = OpAdd;
                status = OTA_ImageRangeValid(u32Addr, u16Left) ? 0 : 0xFF;
                while(u16Left && (status == 0))
                {
                    uint16_t u16PageLeft = (uint16_t)(256 - (u32Addr & 0xFF));
                    uint16_t u16Len = (u16Left < u16PageLeft) ? u16Left : u16PageLeft;
                    status = FLASH_ROM_WRITE(u32Addr, pData, u16Len);
                    u32Addr += u16Len;
                    pData += u16Len;
                    u16Left -= u16Len;
                }
            }
            if(status)             PRINT("IAP_PROM err \r\n");
            OTA_IAP_SendCMDDealSta(status);
            break;
        }
        /* 擦除 -- 蓝牙擦除由主机控制 */
        case CMD_IAP_ERASE:
        {
            OpAdd = (uint32_t)(iap_rec_data.erase.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.erase.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;

            EraseBlockNum = (uint32_t)(iap_rec_data.erase.block_num[0]);
            EraseBlockNum |= ((uint32_t)(iap_rec_data.erase.block_num[1]) << 8);
            EraseAdd = OpAdd;
            EraseBlockCnt = 0;

            /* 检验就放在擦除里清0 */
            VerifyStatus = 0;

            PRINT("IAP_ERASE start:%08x num:%d\r\n", (int)OpAdd, (int)EraseBlockNum);

            if(!OTA_EraseRangeValid(EraseAdd, EraseBlockNum))
            {
                OTA_IAP_SendCMDDealSta(0xFF);
            }
            else
            {
                /* 启动擦除 */
                tmos_set_event(Peripheral_TaskID, OTA_FLASH_ERASE_EVT);
            }
            break;
        }
        /* 校验 */
        case CMD_IAP_VERIFY:
        {
            uint32_t i;
            uint8_t  status = 0;

            OpParaDataLen = iap_rec_data.verify.len;

            OpAdd = (uint32_t)(iap_rec_data.verify.addr[0]);
            OpAdd |= ((uint32_t)(iap_rec_data.verify.addr[1]) << 8);
            OpAdd = OpAdd * 16;

            OpAdd += IMAGE_A_SIZE;
            PRINT("IAP_VERIFY: %08x len:%d \r\n", (int)OpAdd, (int)OpParaDataLen);

            /* 当前是ImageA，直接读取ImageB校验 */
            if(!OTA_ImageRangeValid(OpAdd, OpParaDataLen))
            {
                status = 0xFF;
            }
            else
            {
                status = FLASH_ROM_VERIFY(OpAdd, iap_rec_data.verify.buf, OpParaDataLen);
            }
            if(status)
            {
                PRINT("IAP_VERIFY err \r\n");
            }
            VerifyStatus |= status;
            OTA_IAP_SendCMDDealSta(VerifyStatus);
            break;
        }
        /* 编程结束 */
        case CMD_IAP_END:
        {
            PRINT("IAP_END \r\n");

            /* 当前的是ImageA */
            /* 关闭当前所有使用中断，或者方便一点直接全部关闭 */
            DisableAllIRQ();

            /* 修改DataFlash，切换至ImageIAP */
            SwitchImageFlag(IMAGE_IAP_FLAG);

            /* 等待打印完成 ，复位*/
            mDelaymS(10);
            SYS_ResetExecute();

            break;
        }
        case CMD_IAP_INFO:
        {
            uint8_t send_buf[20] = {0};

            PRINT("IAP_INFO \r\n");

            /* IMAGE FLAG */
            send_buf[0] = IMAGE_B_FLAG;

            /* IMAGE_SIZE */
            send_buf[1] = (uint8_t)(IMAGE_SIZE & 0xff);
            send_buf[2] = (uint8_t)((IMAGE_SIZE >> 8) & 0xff);
            send_buf[3] = (uint8_t)((IMAGE_SIZE >> 16) & 0xff);
            send_buf[4] = (uint8_t)((IMAGE_SIZE >> 24) & 0xff);

            /* BLOCK SIZE */
            send_buf[5] = (uint8_t)(FLASH_BLOCK_SIZE & 0xff);
            send_buf[6] = (uint8_t)((FLASH_BLOCK_SIZE >> 8) & 0xff);

            send_buf[7] = CHIP_ID&0xFF;
            send_buf[8] = (CHIP_ID>>8)&0xFF;
            /* caps: bit0 = PROGRAM splits at 256-byte pages (fw >= 1.2.9) */
            send_buf[9] = 0x0D; /* bit0 page split, bit1 CRC32, bit2 reserved, bit3 set-threshold */
            send_buf[10] = 8;   /* OTA protocol version: fast table-driven CRC32 */
            GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeIN_Floating);
            sample_battery_voltage();
            send_buf[11] = (uint8_t)(Broadcaster_GetBatteryMv() & 0xFF);
            send_buf[12] = (uint8_t)((Broadcaster_GetBatteryMv() >> 8) & 0xFF);
            /* current pulse threshold (fw >= 1.4.1) */
            send_buf[13] = (uint8_t)(Broadcaster_GetThreshold() & 0xFF);
            send_buf[14] = (uint8_t)((Broadcaster_GetThreshold() >> 8) & 0xFF);
            /* 有需要再增加 */

            /* 发送信息 */
            OTA_IAP_SendData(send_buf, 20);

            break;
        }

        /* CRC32 over Image B */
        case CMD_IAP_CRC32:
        {
            uint8_t send_buf[8];
            uint32_t crc = OTA_Crc32Image();
            send_buf[0] = (uint8_t)(crc & 0xFF);
            send_buf[1] = (uint8_t)((crc >> 8) & 0xFF);
            send_buf[2] = (uint8_t)((crc >> 16) & 0xFF);
            send_buf[3] = (uint8_t)((crc >> 24) & 0xFF);
            send_buf[4] = 0;
            send_buf[5] = 0;
            OTA_IAP_SendData(send_buf, 6);
            break;
        }
        /* set the pulse threshold: [cmd,len,threshold_le16,magic_le16] */
        case CMD_IAP_SET_THRESHOLD:
        {
            uint8_t *p = iap_rec_data.other.buf;
            uint16_t value;
            uint8_t status;

            value = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
            if(!OTA_ThresholdMagicValid(p, 6))
            {
                OTA_IAP_SendCMDDealSta(0xFE);
                break;
            }
            status = Broadcaster_SetThreshold(value);
            OTA_IAP_SendCMDDealSta(status);
            break;
        }
        default:
        {
            OTA_IAP_CMDErrDeal();
            break;
        }
    }
}

/*********************************************************************
 * @fn      OTA_IAPReadDataComplete
 *
 * @brief   OTA 数据读取完成处理
 *
 * @param   index   - OTA 通道序号
 *
 * @return  none
 */
void OTA_IAPReadDataComplete(unsigned char index)
{
    PRINT("OTA Send Comp \r\n");
}

/*********************************************************************
 * @fn      OTA_IAPWriteData
 *
 * @brief   OTA 通道数据接收完成处理
 *
 * @param   index   - OTA 通道序号
 * @param   p_data  - 写入的数据
 * @param   w_len   - 写入的长度
 *
 * @return  none
 */
void OTA_IAPWriteData(unsigned char index, unsigned char *p_data, unsigned char w_len)
{
    unsigned char  rec_len;
    unsigned char *rec_data;

    if(s_bNormalMode)
    {
        if(!OTA_RemoteEnterFrameValid(p_data, w_len))
        {
            OTA_IAP_CMDErrDeal();
            return;
        }

        OTA_IAP_SendCMDDealSta(0);
        if(RemoteOtaControlRequestReset(&s_remoteOta))
        {
            OtaMode_Request();
            tmos_stop_task(Peripheral_TaskID, OTA_REMOTE_TIMEOUT_EVT);
            tmos_start_task(Peripheral_TaskID, OTA_REMOTE_RESET_EVT,
                            REMOTE_OTA_RESET_DELAY_TICKS);
        }
        return;
    }

    if((w_len == 0) || !OTA_CommandFrameValid(p_data[0], w_len, (w_len > 1) ? p_data[1] : 0))
    {
        OTA_IAP_CMDErrDeal();
        return;
    }

    rec_len = w_len;
    rec_data = p_data;
    tmos_memcpy((unsigned char *)&iap_rec_data, rec_data, rec_len);
    Rec_OTA_IAP_DataDeal();
}

/*********************************************************************
*********************************************************************/
