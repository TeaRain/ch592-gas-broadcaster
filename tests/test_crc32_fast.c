#include <stdint.h>
#include <stdio.h>
#include "ota_crc32.h"

#define IMAGE_BYTES 221184UL

static uint32_t ReferenceCrc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    uint8_t bit;
    for(i = 0; i < length; i++)
    {
        crc ^= data[i];
        for(bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

int main(void)
{
    static uint8_t data[IMAGE_BYTES];
    static const uint32_t lengths[] = {0, 1, 15, 16, 255, 256, 4096, IMAGE_BYTES};
    uint32_t i;
    uint32_t index;
    for(i = 0; i < IMAGE_BYTES; i++)
    {
        data[i] = (uint8_t)(i * 37u + (i >> 8));
    }
    for(index = 0; index < sizeof(lengths) / sizeof(lengths[0]); index++)
    {
        uint32_t length = lengths[index];
        uint32_t expected = ReferenceCrc32(data, length);
        uint32_t actual = OtaCrc32(data, length);
        if(actual != expected)
        {
            printf("FAIL length %lu: fast=%08lX reference=%08lX\n",
                   (unsigned long)length,
                   (unsigned long)actual,
                   (unsigned long)expected);
            return 1;
        }
    }
    puts("Fast CRC32 matches the bitwise reference at all test lengths");
    return 0;
}
