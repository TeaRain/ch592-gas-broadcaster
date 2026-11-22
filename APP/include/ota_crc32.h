#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

/* zlib-compatible CRC32 using two 4-bit table lookups per byte. The 64-byte
 * table keeps flash cost low while avoiding the old eight-iteration bit loop
 * that blocked the BLE task long enough to drop some proxy connections. */
static uint32_t OtaCrc32(const uint8_t *data, uint32_t length)
{
    static const uint32_t table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    for(i = 0; i < length; i++)
    {
        crc ^= data[i];
        crc = (crc >> 4) ^ table[crc & 0x0Fu];
        crc = (crc >> 4) ^ table[crc & 0x0Fu];
    }
    return ~crc;
}

#endif
