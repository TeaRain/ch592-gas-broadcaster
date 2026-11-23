#include <stdint.h>
#include <stdio.h>

#define EEPROM_BLOCK_SIZE 4096
#include "ota_validation.h"
#include "remote_ota_policy.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_radio_and_reset_budget(void)
{
    CHECK(REMOTE_OTA_ADV_INTERVAL_TICKS == 80);  /* 50 ms */
    CHECK(REMOTE_OTA_ADV_WINDOW_TICKS == 480);   /* 300 ms */
    CHECK(REMOTE_OTA_RESET_DELAY_TICKS == 80);   /* 50 ms */
}

static void test_only_exact_enter_frame_is_accepted(void)
{
    uint8_t valid[6] = {0x88, 0x00, 0xA5, 0x5A, 0x4F, 0x54};
    uint8_t invalid[6];

    CHECK(OTA_RemoteEnterFrameValid(valid, sizeof(valid)));
    CHECK(!OTA_RemoteEnterFrameValid(valid, 5));
    CHECK(!OTA_RemoteEnterFrameValid(valid, 7));
    CHECK(!OTA_RemoteEnterFrameValid(0, 6));

    invalid[0] = CMD_IAP_PROM;
    invalid[1] = 0;
    invalid[2] = 0xA5;
    invalid[3] = 0x5A;
    invalid[4] = 0x4F;
    invalid[5] = 0x54;
    CHECK(!OTA_RemoteEnterFrameValid(invalid, sizeof(invalid)));

    invalid[0] = CMD_IAP_ENTER_OTA;
    invalid[2] = 0;
    CHECK(!OTA_RemoteEnterFrameValid(invalid, sizeof(invalid)));
    invalid[2] = 0xA5;
    invalid[1] = 1;
    CHECK(!OTA_RemoteEnterFrameValid(invalid, sizeof(invalid)));

    /* The normal-mode wake command must not become an OTA-mode command. */
    CHECK(!OTA_CommandFrameValid(CMD_IAP_ENTER_OTA, 6, 0));
}

static void test_connected_normal_link_outlives_advertising_window(void)
{
    CHECK(RemoteOtaMayStopAdvertising(0, 0));
    CHECK(RemoteOtaMayStopAdvertising(0, 1));
    CHECK(RemoteOtaMayStopAdvertising(1, 0));
    CHECK(!RemoteOtaMayStopAdvertising(1, 1));
}

int main(void)
{
    test_radio_and_reset_budget();
    test_only_exact_enter_frame_is_accepted();
    test_connected_normal_link_outlives_advertising_window();
    if (failures) {
        printf("%d remote OTA policy checks failed\n", failures);
        return 1;
    }
    puts("Remote OTA policy checks passed");
    return 0;
}
