#ifndef OTA_VALIDATION_H
#define OTA_VALIDATION_H

#include <stdint.h>
#include "ota.h"

static inline int OTA_CommandFrameValid(uint8_t cmd, uint16_t frame_len, uint8_t data_len)
{
    if(frame_len < 2 || frame_len > IAP_LEN)
    {
        return 0;
    }

    switch(cmd)
    {
        case CMD_IAP_PROM:
        case CMD_IAP_VERIFY:
            return data_len > 0 && data_len <= (IAP_LEN - 4) &&
                   frame_len == (uint16_t)data_len + 4;
        case CMD_IAP_ERASE:
            return frame_len == 6 && data_len == 0;
        case CMD_IAP_END:
        case CMD_IAP_INFO:
        case CMD_IAP_CRC32:
            return frame_len == 2 && data_len == 0;
        case CMD_IAP_SET_GAS:
            return frame_len == 8 && data_len == 0;
        case CMD_IAP_SET_THRESHOLD:
            return frame_len == 6 && data_len == 0;
        default:
            return 0;
    }
}

static inline int OTA_ImageRangeValid(uint32_t addr, uint32_t len)
{
    return len > 0 && addr >= IMAGE_B_START_ADD && addr < IMAGE_IAP_START_ADD &&
           len <= (uint32_t)(IMAGE_IAP_START_ADD - addr);
}

static inline int OTA_EraseRangeValid(uint32_t addr, uint32_t blocks)
{
    return blocks > 0 && addr >= IMAGE_B_START_ADD && addr < IMAGE_IAP_START_ADD &&
           (addr % FLASH_BLOCK_SIZE) == 0 &&
           blocks <= (uint32_t)(IMAGE_IAP_START_ADD - addr) / FLASH_BLOCK_SIZE;
}

static inline int OTA_ThresholdMagicValid(const uint8_t *frame, uint16_t frame_len)
{
    return frame_len == 6 && frame[0] == CMD_IAP_SET_THRESHOLD &&
           frame[4] == 0xA5 && frame[5] == 0x5A;
}

#endif
