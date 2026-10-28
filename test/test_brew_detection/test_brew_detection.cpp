// Software brew detection (BREWDETECTION 1): TempRateFilter + isSoftwareBrewStart,
// checked on synthetic ramps and on real sensor logs from the Silvia.
#include <unity.h>

#include <cstdio>

#include "TempRateFilter.h"
#include "recorded_shots.h"

namespace {

const unsigned long sampleMs = 400;  // intervaltempmestsic
const double setPoint = 95;
const double sensitivity = 120;      // PID_BD_BREWSENSITIVITY default
const unsigned long bdTimeMs = 30000; // PID_BD_TIME on the Silvia

// feed a linear ramp starting at temp0, return the last filter output
double ramp(TempRateFilter &filter, double temp0, double degPerSecond, int samples, unsigned long &now) {
    double rate = 0;
    for (int i = 0; i < samples; i++) {
        now += sampleMs;
        rate = filter.add(now, temp0 + degPerSecond * i * sampleMs / 1000.0);
    }
    return rate;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_constant_temperature_has_zero_rate() {
    TempRateFilter filter;
    filter.reset(95);
    unsigned long now = 10000;
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, ramp(filter, 95, 0, 100, now));
}

void test_rate_unit_is_millidegrees_per_second() {
    TempRateFilter filter;
    filter.reset(95);
    unsigned long now = 10000;
    TEST_ASSERT_DOUBLE_WITHIN(0.5, -200, ramp(filter, 95, -0.2, 60, now));
    TEST_ASSERT_DOUBLE_WITHIN(0.5, 500, ramp(filter, 90, 0.5, 60, now));
}

void test_start_condition() {
    TEST_ASSERT_TRUE(isSoftwareBrewStart(-121, 93, setPoint, sensitivity));
    TEST_ASSERT_TRUE(isSoftwareBrewStart(-120, 93, setPoint, sensitivity));
    TEST_ASSERT_FALSE(isSoftwareBrewStart(-119, 93, setPoint, sensitivity));
    // only near the set point: no detection while heating up or after steam
    TEST_ASSERT_FALSE(isSoftwareBrewStart(-500, 89.9, setPoint, sensitivity));
    TEST_ASSERT_FALSE(isSoftwareBrewStart(-500, 100.1, setPoint, sensitivity));
}

void test_slow_cooling_does_not_trigger() {
    // e.g. a long hot-water draw: ~0.03 °C/s for minutes
    TempRateFilter filter;
    filter.reset(95);
    unsigned long now = 10000;
    for (int i = 0; i < 600; i++) {
        now += sampleMs;
        double temp = 95 - 0.03 * i * sampleMs / 1000.0;
        TEST_ASSERT_FALSE(isSoftwareBrewStart(filter.add(now, temp), temp, setPoint, sensitivity));
    }
}

void test_recorded_shots_detected_once_and_quickly() {
    for (const RecordedShot &shot : recordedShots) {
        TempRateFilter filter;
        filter.reset(shot.tenths[0] / 10.0);
        unsigned long detectedAt = 0;
        bool detected = false;
        int hits[8] = {};
        int falseTriggers = 0;

        for (int i = 0; i < shot.count; i++) {
            unsigned long now = i * sampleMs;
            double temp = shot.tenths[i] / 10.0;
            double rate = filter.add(now + 1000000, temp);
            if (detected && now - detectedAt > bdTimeMs) detected = false;
            if (!detected && isSoftwareBrewStart(rate, temp, setPoint, sensitivity)) {
                detected = true;
                detectedAt = now;
                bool matched = false;
                for (int d = 0; d < shot.drawCount; d++) {
                    long latency = (long)now - shot.drawStartsMs[d];
                    if (latency >= -2000 && latency <= 8000) {
                        hits[d]++;
                        matched = true;
                    }
                }
                if (!matched) falseTriggers++;
            }
        }

        char msg[96];
        for (int d = 0; d < shot.drawCount; d++) {
            snprintf(msg, sizeof msg, "%s: draw %d at %d ms detected %d times", shot.name, d,
                     shot.drawStartsMs[d], hits[d]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, hits[d], msg);
        }
        snprintf(msg, sizeof msg, "%s: false triggers", shot.name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, falseTriggers, msg);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_constant_temperature_has_zero_rate);
    RUN_TEST(test_rate_unit_is_millidegrees_per_second);
    RUN_TEST(test_start_condition);
    RUN_TEST(test_slow_cooling_does_not_trigger);
    RUN_TEST(test_recorded_shots_detected_once_and_quickly);
    return UNITY_END();
}
