#include <stdint.h>
#include <stdio.h>

#define EEPROM_BLOCK_SIZE 4096
#include "remote_ota_policy.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static uint8_t normal_may_stop(RemoteOtaControlState *state)
{
    return (uint8_t)RemoteOtaMayStopAdvertising(
        1, state->connected || state->reset_pending
    );
}

static void test_valid_wake_schedules_one_delayed_reset(void)
{
    RemoteOtaControlState state;
    const uint8_t valid[6] = {0x88, 0x00, 0xA5, 0x5A, 0x4F, 0x54};

    RemoteOtaControlInit(&state);
    CHECK(!state.connected);
    CHECK(!state.reset_pending);
    CHECK(normal_may_stop(&state));

    RemoteOtaControlSetConnected(&state, 1);
    CHECK(state.connected);
    CHECK(!normal_may_stop(&state));
    CHECK(REMOTE_OTA_CONTROL_TIMEOUT_TICKS == 8000); /* 5 s */
    CHECK(RemoteOtaControlShouldDisconnect(&state));

    CHECK(OTA_RemoteEnterFrameValid(valid, sizeof(valid)));
    CHECK(RemoteOtaControlRequestReset(&state) == 1);
    CHECK(state.reset_pending);
    CHECK(RemoteOtaControlRequestReset(&state) == 0);
    CHECK(!RemoteOtaControlShouldDisconnect(&state));

    RemoteOtaControlSetConnected(&state, 0);
    CHECK(!state.connected);
    CHECK(!normal_may_stop(&state));
}

static void test_connected_advertising_uses_same_bounded_connection(void)
{
    RemoteOtaControlState state;

    RemoteOtaControlInit(&state);
    CHECK(state.connection_handle == 0xFFFF);
    RemoteOtaControlSetConnection(&state, 0x0042);
    CHECK(state.connected);
    CHECK(state.connection_handle == 0x0042);
    CHECK(RemoteOtaControlShouldDisconnect(&state));
    RemoteOtaControlSetConnected(&state, 0);
    CHECK(state.connection_handle == 0xFFFF);
    CHECK(!RemoteOtaControlShouldDisconnect(&state));
}

static void test_invalid_commands_do_not_arm_reset(void)
{
    RemoteOtaControlState state;
    const uint8_t program[6] = {CMD_IAP_PROM, 0, 0xA5, 0x5A, 0x4F, 0x54};
    const uint8_t bad_magic[6] = {CMD_IAP_ENTER_OTA, 0, 0, 0x5A, 0x4F, 0x54};

    RemoteOtaControlInit(&state);
    if (OTA_RemoteEnterFrameValid(program, sizeof(program))) {
        CHECK(0);
    }
    if (OTA_RemoteEnterFrameValid(bad_magic, sizeof(bad_magic))) {
        CHECK(0);
    }
    CHECK(!state.reset_pending);
    CHECK(normal_may_stop(&state));
}

int main(void)
{
    test_valid_wake_schedules_one_delayed_reset();
    test_connected_advertising_uses_same_bounded_connection();
    test_invalid_commands_do_not_arm_reset();
    if (failures) {
        printf("%d remote OTA runtime checks failed\n", failures);
        return 1;
    }
    puts("Remote OTA runtime checks passed");
    return 0;
}
