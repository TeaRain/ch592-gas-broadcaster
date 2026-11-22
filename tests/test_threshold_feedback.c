#include <stdint.h>
#include <stdio.h>
#include "bthome_threshold.h"

static int failures;
static uint8_t packet[BthomeAdvertSize(5)];
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

int main(void)
{
    CHECK(BTHOME_THRESHOLD_OFFSET == 19);
    CHECK(BTHOME_VERSION_OFFSET == 24);
    CHECK(BthomeAdvertDataLength(5) == 27);
    CHECK(BthomeAdvertSize(5) == 31);

    BthomeWriteThreshold(packet, 20);
    CHECK(packet[19] == 0x3E);
    CHECK(packet[20] == 20 && packet[21] == 0 && packet[22] == 0 && packet[23] == 0);

    BthomeWriteThreshold(packet, 2000);
    CHECK(packet[19] == 0x3E);
    CHECK(packet[20] == 0xD0 && packet[21] == 0x07);
    CHECK(packet[22] == 0 && packet[23] == 0);

    if (failures) return 1;
    puts("Threshold BTHome layout and uint32 encoding checks passed");
    return 0;
}
