#ifndef SCHEDULED_OTA_H
#define SCHEDULED_OTA_H

#include <stdint.h>

#define SCHEDULED_OTA_INTERVAL_MINUTES 4320UL
#define OTA_REQUEST_MANUAL 0
#define OTA_REQUEST_SCHEDULED 1
#define OTA_MANUAL_IDLE_TICKS 480
#define OTA_SCHEDULED_IDLE_TICKS 96

static inline int ScheduledOta_ShouldRequest(uint32_t elapsed_minutes)
{
    return elapsed_minutes >= SCHEDULED_OTA_INTERVAL_MINUTES;
}

static inline int OtaRequestReasonIsScheduled(uint8_t reason)
{
    return reason == OTA_REQUEST_SCHEDULED;
}

static inline uint16_t OtaIdleLimitTicks(uint8_t reason)
{
    return OtaRequestReasonIsScheduled(reason)
               ? OTA_SCHEDULED_IDLE_TICKS
               : OTA_MANUAL_IDLE_TICKS;
}

#endif
