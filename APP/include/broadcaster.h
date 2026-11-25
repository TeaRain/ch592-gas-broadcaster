/********************************** (C) COPYRIGHT *******************************
 * File Name          : broadcaster.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef BROADCASTER_H
#define BROADCASTER_H

#include "remote_ota_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */

/*********************************************************************
 * CONSTANTS
 */
// Simple BLE Broadcaster Task Events
#define SBP_START_DEVICE_EVT          0x0001  // 启动设备事件
#define SBP_GAS_METER_EVT             0x0002  // 燃气检测事件
#define SBP_BROADCASTER_EVT           0x0004  // 广播使能事件
#define SBP_ADVERT_DISABLE_EVT        0x0008  // 关闭广播事件

#define SBP_GAS_METER_EVT_PERIOD      480     // 300ms
#define SBP_BROADCASTER_EVT_PERIOD    96000   // 1分钟  
#define SBP_ADVERT_INTERVAL           REMOTE_OTA_ADV_INTERVAL_TICKS  // 50 ms
#define SBP_ADVERT_DISABLE_EVT_PERIOD REMOTE_OTA_ADV_WINDOW_TICKS     // 300 ms
#define SBP_ADVERT_DISABLE_EVT_TIME   1       // 广播窗口倍数

#define ADC_VREF                     1050     // 1.05V
#define DEFAULT_GAS_METER            0        // 初始燃气值

#define GAS_METER_SAVE_STEP          100                      // 每变化0.1立方米自动保存
#define TCRT5000_THRESHOLD_DEFAULT   100      // default pulse threshold (adjustable over OTA)
#define TCRT5000_THRESHOLD_MIN       20       // lowest threshold the OTA command accepts
#define TCRT5000_THRESHOLD_MAX       2000     // highest threshold the OTA command accepts
#define TCRT5000_MIN_THRESHOLD       50       // 采样最小值更新阈值
#define FW_VERSION_STRING            "1.6.2"  // BTHome text object 0x53
#define OTA_MODE_FLASH_ADDR          0x2000   // DataFlash block for OTA-mode request
#define OTA_MODE_MAGIC               0xA5

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * FUNCTIONS
 */

/*
 * Task Initialization for the BLE Broadcaster Application
 */
extern void Broadcaster_Init(void);
extern uint8_t OtaMode_IsRequested(void);
extern void OtaMode_ClearRequest(void);
extern void OtaMode_Request(void);
extern void OtaMode_RequestScheduled(void);
extern uint8_t OtaMode_IsScheduledRequest(void);
/* write the gas reading from the OTA channel (0.001 m3), 0 = ok */
/* runtime pulse threshold, stored in DataFlash block 3 */
extern uint16_t Broadcaster_GetThreshold(void);
extern uint8_t Broadcaster_SetThreshold(uint16_t value);
extern void ReadConfigFromDataFlash(void);

/*
 * Task Event Processor for the BLE Broadcaster Application
 */
extern uint16_t Broadcaster_ProcessEvent(uint8_t task_id, uint16_t events);

/*********************************************************************
*********************************************************************/

#ifdef __cplusplus
}
#endif

#endif
