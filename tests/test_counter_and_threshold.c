#include <stdint.h>
#include <stdio.h>
#include "gas_counter_policy.h"
#include "threshold_policy.h"
#include "threshold_key_policy.h"

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL line %d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

int main(void)
{
    CHECK(GasCounterDelta(100, 90) == 10);
    CHECK(GasCounterDelta(4, UINT32_MAX - 5) == 10);
    CHECK(GasCounterDelta(0, UINT32_MAX) == 1);

    CHECK(ThresholdStepForHold(0) == 0);
    CHECK(ThresholdStepForHold(9) == 0);
    CHECK(ThresholdStepForHold(10) == 10);
    CHECK(ThresholdStepForHold(29) == 10);
    CHECK(ThresholdStepForHold(30) == 50);
    CHECK(ThresholdStepForHold(100) == 50);

    CHECK(ThresholdClamp(0, 20, 2000) == 20);
    CHECK(ThresholdClamp(100, 20, 2000) == 100);
    CHECK(ThresholdClamp(3000, 20, 2000) == 2000);

    {
        ThresholdKeyState state = {0};
        int i;
        ThresholdKeyStart(&state, THRESHOLD_KEY_P);
        CHECK(ThresholdKeyHold(&state) == 0);
        CHECK(ThresholdKeyRelease(&state) == 1);
        CHECK(state.active == 0);

        ThresholdKeyStart(&state, THRESHOLD_KEY_N);
        CHECK(ThresholdKeyHold(&state) == 0);
        CHECK(ThresholdKeyRelease(&state) == -1);

        ThresholdKeyStart(&state, THRESHOLD_KEY_P);
        for (i = 0; i < 10; i++) (void)ThresholdKeyHold(&state);
        CHECK(ThresholdKeyRelease(&state) == 0);
    }

    if (failures) return 1;
    puts("Counter and threshold policy checks passed");
    return 0;
}
