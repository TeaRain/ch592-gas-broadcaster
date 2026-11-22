#ifndef GAS_COUNTER_POLICY_H
#define GAS_COUNTER_POLICY_H

#include <stdint.h>

static inline uint32_t GasCounterDelta(uint32_t current, uint32_t saved)
{
    return current - saved;
}

#endif
