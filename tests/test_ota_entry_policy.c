#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_entry_policy.h"

static int failures;
static char events[8];
static uint8_t event_count;
static uint8_t reasons[4];
static uint8_t reason_count;

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL line %d: %s\n", __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void reset_events(void)
{
    memset(events, 0, sizeof(events));
    memset(reasons, 0, sizeof(reasons));
    event_count = 0;
    reason_count = 0;
}

static void record_save(void)
{
    if (event_count < sizeof(events)) events[event_count++] = 'S';
}

static void record_write(uint8_t reason)
{
    if (event_count < sizeof(events)) events[event_count++] = 'W';
    if (reason_count < sizeof(reasons)) reasons[reason_count++] = reason;
}

static void test_save_precedes_manual_request(void)
{
    reset_events();
    OtaEntryFlushBeforeRequest(record_save, record_write, 0);
    CHECK(event_count == 2);
    CHECK(memcmp(events, "SW", 2) == 0);
    CHECK(reason_count == 1);
    CHECK(reasons[0] == 0);
}

static void test_save_precedes_scheduled_request(void)
{
    reset_events();
    OtaEntryFlushBeforeRequest(record_save, record_write, 1);
    CHECK(event_count == 2);
    CHECK(memcmp(events, "SW", 2) == 0);
    CHECK(reason_count == 1);
    CHECK(reasons[0] == 1);
}

static void test_every_request_flushes(void)
{
    reset_events();
    OtaEntryFlushBeforeRequest(record_save, record_write, 0);
    OtaEntryFlushBeforeRequest(record_save, record_write, 1);
    CHECK(event_count == 4);
    CHECK(memcmp(events, "SWSW", 4) == 0);
    CHECK(reason_count == 2);
    CHECK(reasons[0] == 0 && reasons[1] == 1);
}

int main(void)
{
    test_save_precedes_manual_request();
    test_save_precedes_scheduled_request();
    test_every_request_flushes();
    if (failures) return 1;
    puts("OTA entry flush ordering checks passed");
    return 0;
}
