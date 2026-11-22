#ifndef THRESHOLD_KEY_POLICY_H
#define THRESHOLD_KEY_POLICY_H

#include <stdint.h>
#include "threshold_policy.h"

#define THRESHOLD_KEY_P 1
#define THRESHOLD_KEY_N 2

typedef struct
{
    uint8_t active;
    uint8_t direction;
    uint16_t down_ticks;
} ThresholdKeyState;

static inline void ThresholdKeyStart(ThresholdKeyState *state, uint8_t direction)
{
    if(!state->active)
    {
        state->active = 1;
        state->direction = direction;
        state->down_ticks = 0;
    }
}

static inline uint16_t ThresholdKeyHold(ThresholdKeyState *state)
{
    if(!state->active) return 0;
    if(state->down_ticks < 0xFFFF) state->down_ticks++;
    return ThresholdStepForHold(state->down_ticks);
}

static inline int16_t ThresholdKeyRelease(ThresholdKeyState *state)
{
    int16_t delta = 0;
    if(state->active && state->down_ticks == 0)
    {
        delta = (state->direction == THRESHOLD_KEY_P) ? 1 : -1;
    }
    state->active = 0;
    state->direction = 0;
    state->down_ticks = 0;
    return delta;
}

#endif
