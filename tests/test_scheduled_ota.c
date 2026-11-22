#include <stdint.h>
#include <stdio.h>
#include "scheduled_ota.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

int main(void)
{
    CHECK(!ScheduledOta_ShouldRequest(0));
    CHECK(!ScheduledOta_ShouldRequest(4319));
    CHECK(ScheduledOta_ShouldRequest(4320));
    CHECK(ScheduledOta_ShouldRequest(4321));
    CHECK(ScheduledOta_ShouldRequest(UINT32_MAX));

    CHECK(!OtaRequestReasonIsScheduled(OTA_REQUEST_MANUAL));
    CHECK(OtaRequestReasonIsScheduled(OTA_REQUEST_SCHEDULED));
    CHECK(!OtaRequestReasonIsScheduled(0xFF));

    CHECK(OtaIdleLimitTicks(OTA_REQUEST_MANUAL) == 480);
    CHECK(OtaIdleLimitTicks(OTA_REQUEST_SCHEDULED) == 96);
    CHECK(OtaIdleLimitTicks(0xFF) == 480);

    if (failures) return 1;
    puts("Scheduled OTA policy checks passed");
    return 0;
}
