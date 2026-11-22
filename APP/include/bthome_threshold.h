#ifndef BTHOME_THRESHOLD_H
#define BTHOME_THRESHOLD_H

#include <stdint.h>

#define BTHOME_THRESHOLD_OFFSET 19
#define BTHOME_VERSION_OFFSET 24

#define BthomeAdvertDataLength(version_length) \
    ((uint8_t)(20u + 2u + (version_length)))
#define BthomeAdvertSize(version_length) \
    ((uint8_t)(24u + 2u + (version_length)))

static inline void BthomeWriteThreshold(uint8_t *data, uint16_t threshold)
{
    data[BTHOME_THRESHOLD_OFFSET] = 0x3E; /* BTHome v2 count (uint32) */
    data[BTHOME_THRESHOLD_OFFSET + 1] = (uint8_t)(threshold & 0xFFu);
    data[BTHOME_THRESHOLD_OFFSET + 2] = (uint8_t)((threshold >> 8) & 0xFFu);
    data[BTHOME_THRESHOLD_OFFSET + 3] = 0;
    data[BTHOME_THRESHOLD_OFFSET + 4] = 0;
}

#endif
