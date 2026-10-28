// Pins down how the rancilio-pid PID library behaves with the firmware's settings
// (rancilio-pid.cpp setup(): 1 s sample time, output 0..1000, EMA 0.6, P_ON_E).
#include <Arduino.h>
#include <PID_v1.h>
#include <unity.h>

namespace {

double input, output, setpoint;

struct Fixture {
    PID pid;

    Fixture(double kp, double tn, double tv, double iMax = 75)
        : pid(&input, &output, &setpoint, kp, tn ? kp / tn : 0, tv * kp, P_ON_E, DIRECT) {
        pid.SetSampleTime(1000);
        pid.SetOutputLimits(0, 1000);
        pid.SetSmoothingFactor(0.6);
        pid.SetIntegratorLimits(0, iMax);
        pid.SetMode(AUTOMATIC);
    }

    // advance one sample period and compute
    bool step(double in) {
        input = in;
        fakeMillis += 1000;
        return pid.Compute();
    }
};

}  // namespace

void setUp() {
    fakeMillis = 100000;
    input = output = 0;
    setpoint = 95;
}

void tearDown() {}

void test_proportional_only() {
    Fixture f(62, 0, 0);
    input = 90;
    f.pid.SetMode(MANUAL);
    f.pid.SetMode(AUTOMATIC);  // start with a settled filter at the current input
    f.step(90);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 62 * 5, output);
}

void test_output_is_clamped_to_window() {
    Fixture f(62, 0, 0);
    f.step(50);
    TEST_ASSERT_EQUAL_DOUBLE(1000, output);
    f.step(120);
    TEST_ASSERT_EQUAL_DOUBLE(0, output);
}

void test_compute_waits_for_sample_time() {
    Fixture f(62, 52, 0);
    TEST_ASSERT_TRUE(f.step(94));
    input = 80;
    fakeMillis += 500;
    TEST_ASSERT_FALSE(f.pid.Compute());
}

void test_integrator_stops_at_i_max() {
    // holding power at 95 C is ~3-6 %, i.e. the integrator alone should never exceed PID_I_MAX
    Fixture f(62, 52, 0, 75);
    // Ki * error = 62/52 * 0.1 per second -> 75 is reached after ~630 s
    for (int i = 0; i < 1200; i++) f.step(94.9);
    TEST_ASSERT_DOUBLE_WITHIN(1e-3, 62 * 0.1 + 75, output);
}

void test_integrator_frozen_while_output_saturated() {
    Fixture f(62, 52, 0, 1000);
    for (int i = 0; i < 30; i++) f.step(70);  // 25 C below: P alone saturates
    TEST_ASSERT_EQUAL_DOUBLE(1000, output);
    f.step(95);  // at setpoint the remaining output is the integrator only
    TEST_ASSERT_TRUE_MESSAGE(output < 62 * 25.0 / 52 * 2,
                             "integrator wound up although the output was saturated");
}

void test_zero_ki_resets_integrator() {
    // this is what happens when the brew PID (Tn = 0) takes over
    Fixture f(62, 52, 0, 75);
    for (int i = 0; i < 600; i++) f.step(94.9);
    f.pid.SetTunings(120, 0, 0, P_ON_E);
    f.step(95);
    TEST_ASSERT_EQUAL_DOUBLE(0, output);
}

void test_manual_mode_freezes_output_instead_of_stopping_heating() {
    // BREWPID_DELAY > 0 puts the PID into MANUAL: the heater keeps the last output
    Fixture f(62, 52, 0);
    f.step(90);
    double before = output;
    TEST_ASSERT_TRUE(before > 0);
    f.pid.SetMode(MANUAL);
    for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE(f.step(85));
    TEST_ASSERT_EQUAL_DOUBLE(before, output);
}

void test_back_to_auto_is_bumpless_but_respects_i_max() {
    Fixture f(62, 52, 0, 75);
    f.step(90);  // output 310
    f.pid.SetMode(MANUAL);
    f.pid.SetMode(AUTOMATIC);  // integrator = 310
    f.step(95);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 75 + 62.0 / 52 * 0, output);
}

void test_derivative_acts_on_falling_temperature_not_on_setpoint() {
    Fixture f(62, 0, 20);
    input = 95;
    f.pid.SetMode(MANUAL);
    f.pid.SetMode(AUTOMATIC);
    f.step(95);
    TEST_ASSERT_EQUAL_DOUBLE(0, output);

    setpoint = 100;  // setpoint jump: P reacts, D must not kick
    f.step(95);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 62 * 5, output);

    setpoint = 95;
    f.step(94);  // falling: D adds heat on top of P
    TEST_ASSERT_TRUE(output > 62 * 1);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_proportional_only);
    RUN_TEST(test_output_is_clamped_to_window);
    RUN_TEST(test_compute_waits_for_sample_time);
    RUN_TEST(test_integrator_stops_at_i_max);
    RUN_TEST(test_integrator_frozen_while_output_saturated);
    RUN_TEST(test_zero_ki_resets_integrator);
    RUN_TEST(test_manual_mode_freezes_output_instead_of_stopping_heating);
    RUN_TEST(test_back_to_auto_is_bumpless_but_respects_i_max);
    RUN_TEST(test_derivative_acts_on_falling_temperature_not_on_setpoint);
    return UNITY_END();
}
