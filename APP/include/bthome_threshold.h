#ifndef BTHOME_THRESHOLD_H
#define BTHOME_THRESHOLD_H

#include <stdint.h>

#define BTHOME_THRESHOLD_OFFSET 14
#define BTHOME_GAS_OFFSET 19
#define BTHOME_VERSION_OFFSET 24

#define BthomeAdvertDataLength(version_length) \
    ((uint8_t)(20u + 2u + (version_length)))
#define BthomeAdvertSize(version_length) \
    ((uint8_t)(24u + 2u + (version_length)))

#define BTHOME_DATA_INITIALISER \
{ \
    0x02, 0x01, 0x06, \
    0x0F, 0x16, 0xD2, 0xFC, 0x40, \
    0x0C, 0x00, 0x00, \
    0x3D, 0x00, 0x00, \
    0x3E, 0x00, 0x00, 0x00, 0x00, \
    0x4C, 0x00, 0x00, 0x00, 0x00 \
}

static inline void BthomeWriteThreshold(uint8_t *data, uint16_t threshold)
{
    data[BTHOME_THRESHOLD_OFFSET] = 0x3E; /* BTHome v2 count (uint32) */
    data[BTHOME_THRESHOLD_OFFSET + 1] = (uint8_t)(threshold & 0xFFu);
    data[BTHOME_THRESHOLD_OFFSET + 2] = (uint8_t)((threshold >> 8) & 0xFFu);
    data[BTHOME_THRESHOLD_OFFSET + 3] = 0;
    data[BTHOME_THRESHOLD_OFFSET + 4] = 0;
}

static inline void BthomeWriteGas(uint8_t *data, uint32_t gas)
{
    data[BTHOME_GAS_OFFSET] = 0x4C; /* BTHome v2 gas (uint32) */
    data[BTHOME_GAS_OFFSET + 1] = (uint8_t)(gas & 0xFFu);
    data[BTHOME_GAS_OFFSET + 2] = (uint8_t)((gas >> 8) & 0xFFu);
    data[BTHOME_GAS_OFFSET + 3] = (uint8_t)((gas >> 16) & 0xFFu);
    data[BTHOME_GAS_OFFSET + 4] = (uint8_t)((gas >> 24) & 0xFFu);
}

#endif
