#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bthome_threshold.h"

static int failures;
static uint8_t packet[BthomeAdvertSize(5)] = BTHOME_DATA_INITIALISER;
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

int main(void)
{
    static const uint8_t expected[BthomeAdvertSize(5)] = {
        0x02, 0x01, 0x06,
        0x1B, 0x16, 0xD2, 0xFC, 0x40,
        0x0C, 0x80, 0x0B,
        0x3D, 0xEB, 0x03,
        0x3E, 0x14, 0x00, 0x00, 0x00,
        0x4C, 0x78, 0x56, 0x34, 0x12,
        0x53, 0x05, 0x31, 0x2E, 0x32, 0x2E, 0x33
    };

    CHECK(BTHOME_THRESHOLD_OFFSET == 14);
    CHECK(BTHOME_VERSION_OFFSET == 24);
    CHECK(BthomeAdvertDataLength(5) == 27);
    CHECK(BthomeAdvertSize(5) == 31);

    packet[3] = BthomeAdvertDataLength(5);
    packet[9] = 0x80;
    packet[10] = 0x0B;
    packet[12] = 0xEB;
    packet[13] = 0x03;
    BthomeWriteThreshold(packet, 20);
    BthomeWriteGas(packet, 0x12345678UL);
    packet[BTHOME_VERSION_OFFSET] = 0x53;
    packet[BTHOME_VERSION_OFFSET + 1] = 5;
    memcpy(&packet[BTHOME_VERSION_OFFSET + 2], "1.2.3", 5);
    CHECK(memcmp(packet, expected, sizeof(expected)) == 0);

    BthomeWriteThreshold(packet, 2000);
    CHECK(packet[14] == 0x3E);
    CHECK(packet[15] == 0xD0 && packet[16] == 0x07);
    CHECK(packet[17] == 0 && packet[18] == 0);
    CHECK(packet[19] == 0x4C);
    CHECK(packet[20] == 0x78 && packet[21] == 0x56);
    CHECK(packet[22] == 0x34 && packet[23] == 0x12);

    if (failures) return 1;
    puts("Threshold BTHome layout and uint32 encoding checks passed");
    return 0;
}
