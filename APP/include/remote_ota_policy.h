#ifndef REMOTE_OTA_POLICY_H
#define REMOTE_OTA_POLICY_H

#include <stdint.h>
#include "ota.h"

/* TMOS units are 625 us: 80 = 50 ms, 480 = 300 ms. */
#define REMOTE_OTA_ADV_INTERVAL_TICKS  80
#define REMOTE_OTA_ADV_WINDOW_TICKS    480
#define REMOTE_OTA_RESET_DELAY_TICKS   80
#define REMOTE_OTA_CONTROL_TIMEOUT_TICKS 8000

typedef struct
{
    uint8_t connected;
    uint8_t reset_pending;
    uint16_t connection_handle;
} RemoteOtaControlState;

static inline void RemoteOtaControlInit(RemoteOtaControlState *state)
{
    if(state != 0)
    {
        state->connected = 0;
        state->reset_pending = 0;
        state->connection_handle = 0xFFFF;
    }
}

static inline void RemoteOtaControlSetConnected(RemoteOtaControlState *state, uint8_t connected)
{
    if(state != 0)
    {
        state->connected = connected ? 1 : 0;
        if(!connected)
        {
            state->connection_handle = 0xFFFF;
        }
    }
}

static inline void RemoteOtaControlSetConnection(RemoteOtaControlState *state, uint16_t handle)
{
    if(state != 0)
    {
        state->connected = 1;
        state->connection_handle = handle;
    }
}

static inline int RemoteOtaControlRequestReset(RemoteOtaControlState *state)
{
    if(state == 0 || state->reset_pending)
    {
        return 0;
    }
    state->reset_pending = 1;
    return 1;
}

static inline int RemoteOtaControlShouldDisconnect(const RemoteOtaControlState *state)
{
    return state != 0 && state->connected && !state->reset_pending;
}

static inline int OTA_RemoteEnterFrameValid(const uint8_t *frame, uint16_t len)
{
    static const uint8_t expected[6] = {
        CMD_IAP_ENTER_OTA, 0x00, 0xA5, 0x5A, 0x4F, 0x54
    };
    uint16_t i;

    if(frame == 0 || len != sizeof(expected))
    {
        return 0;
    }
    for(i = 0; i < sizeof(expected); i++)
    {
        if(frame[i] != expected[i])
        {
            return 0;
        }
    }
    return 1;
}

static inline int RemoteOtaMayStopAdvertising(uint8_t normal_mode, uint8_t connected)
{
    return !normal_mode || !connected;
}

#endif
