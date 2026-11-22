#ifndef THRESHOLD_POLICY_H
#define THRESHOLD_POLICY_H

#include <stdint.h>

static inline uint16_t ThresholdStepForHold(uint16_t down_ticks)
{
    if(down_ticks >= 30) return 50;
    if(down_ticks >= 10) return 10;
    if(down_ticks >= 1) return 1;
    return 0;
}

static inline uint16_t ThresholdClamp(uint32_t value, uint16_t minimum, uint16_t maximum)
{
    if(value < minimum) return minimum;
    if(value > maximum) return maximum;
    return (uint16_t)value;
}

#endif
