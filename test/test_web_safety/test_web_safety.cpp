// POST /parameters input validation and the scoped heater ISR pause used by the web handlers.
#include <unity.h>

#include "ISR.h"
#include "ParameterInput.h"

bool skipHeaterISR = false;

void setUp() { skipHeaterISR = false; }
void tearDown() {}

namespace {

ParamInput parse(const char *name, const char *text, double &value, double min = 20, double max = 110,
                 bool integral = false) {
    return parseParameterValue(name, text, min, max, integral, value);
}

}  // namespace

void test_valid_value_is_accepted() {
    double v = 0;
    TEST_ASSERT_EQUAL(ParamInput::Ok, parse("BREW_SET_POINT", "96", v));
    TEST_ASSERT_EQUAL_DOUBLE(96, v);
    TEST_ASSERT_EQUAL(ParamInput::Ok, parse("BREW_SET_POINT", " 95.5 ", v));
    TEST_ASSERT_EQUAL_DOUBLE(95.5, v);
}

void test_limits_are_inclusive() {
    double v = 0;
    TEST_ASSERT_EQUAL(ParamInput::Ok, parse("BREW_SET_POINT", "20", v));
    TEST_ASSERT_EQUAL(ParamInput::Ok, parse("BREW_SET_POINT", "110", v));
    TEST_ASSERT_EQUAL(ParamInput::OutOfRange, parse("BREW_SET_POINT", "110.1", v));
    TEST_ASSERT_EQUAL(ParamInput::OutOfRange, parse("BREW_SET_POINT", "19.9", v));
}

void test_rejected_value_leaves_target_untouched() {
    double v = 42;
    TEST_ASSERT_EQUAL(ParamInput::OutOfRange, parse("BREW_SET_POINT", "150", v));
    TEST_ASSERT_EQUAL_DOUBLE(42, v);
}

void test_garbage_is_not_read_as_zero() {
    // atof() used to turn these into 0 and write them
    double v = 42;
    TEST_ASSERT_EQUAL(ParamInput::Invalid, parse("PID_KP", "", v, 0, 200));
    TEST_ASSERT_EQUAL(ParamInput::Invalid, parse("PID_KP", "abc", v, 0, 200));
    TEST_ASSERT_EQUAL(ParamInput::Invalid, parse("PID_KP", "62x", v, 0, 200));
    TEST_ASSERT_EQUAL(ParamInput::Invalid, parse("PID_KP", "nan", v, 0, 200));
    TEST_ASSERT_EQUAL_DOUBLE(42, v);
}

void test_integral_parameters_reject_fractions() {
    double v = 0;
    TEST_ASSERT_EQUAL(ParamInput::Ok, parse("PID_ON", "1", v, 0, 1, true));
    TEST_ASSERT_EQUAL(ParamInput::Invalid, parse("PID_ON", "0.5", v, 0, 1, true));
    TEST_ASSERT_EQUAL(ParamInput::OutOfRange, parse("PID_ON", "2", v, 0, 1, true));
}

void test_live_temperature_is_never_written() {
    // the web page posts the TEMP it showed when it was loaded
    double v = 0;
    TEST_ASSERT_EQUAL(ParamInput::ReadOnly, parse("TEMP", "40", v, 0, 200));
    TEST_ASSERT_EQUAL(ParamInput::ReadOnly, parse("VERSION", "3.3.0", v, 0, 1));
    TEST_ASSERT_FALSE(isReadOnlyParameter("TEMPERATURE"));
}

namespace {

bool servePage(bool fragmentFound) {
    HeaterIsrPause pause;
    if (fragmentFound) return true;  // early return, used to leave skipHeaterISR = true
    return false;
}

}  // namespace

void test_isr_pause_ends_on_every_return_path() {
    servePage(true);
    TEST_ASSERT_FALSE(skipHeaterISR);
    servePage(false);
    TEST_ASSERT_FALSE(skipHeaterISR);
}

void test_isr_pause_is_active_inside_scope_and_nests() {
    {
        HeaterIsrPause outer;
        TEST_ASSERT_TRUE(skipHeaterISR);
        {
            HeaterIsrPause inner;  // e.g. storageCommit() inside the POST handler
            TEST_ASSERT_TRUE(skipHeaterISR);
        }
        TEST_ASSERT_TRUE_MESSAGE(skipHeaterISR, "inner pause re-enabled the ISR too early");
    }
    TEST_ASSERT_FALSE(skipHeaterISR);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_value_is_accepted);
    RUN_TEST(test_limits_are_inclusive);
    RUN_TEST(test_rejected_value_leaves_target_untouched);
    RUN_TEST(test_garbage_is_not_read_as_zero);
    RUN_TEST(test_integral_parameters_reject_fractions);
    RUN_TEST(test_live_temperature_is_never_written);
    RUN_TEST(test_isr_pause_ends_on_every_return_path);
    RUN_TEST(test_isr_pause_is_active_inside_scope_and_nests);
    return UNITY_END();
}
