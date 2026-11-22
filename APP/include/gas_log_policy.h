#ifndef GAS_LOG_POLICY_H
#define GAS_LOG_POLICY_H

#include <stdint.h>

#define GAS_LOG_BLOCKS 2
#define GAS_LOG_RECORDS_PER_BLOCK 256

typedef int (*GasLogSequenceReader)(
    uint8_t block, uint16_t index, uint32_t *sequence, void *context
);

static inline uint32_t GasLogRecoverMaxSequence(
    uint32_t current, GasLogSequenceReader reader, void *context
)
{
    uint32_t maximum = current;
    uint8_t block;

    for(block = 0; block < GAS_LOG_BLOCKS; block++)
    {
        uint16_t index;
        for(index = 0; index < GAS_LOG_RECORDS_PER_BLOCK; index++)
        {
            uint32_t sequence;
            if(reader(block, index, &sequence, context) && sequence > maximum)
            {
                maximum = sequence;
            }
        }
    }
    return maximum;
}

#endif
