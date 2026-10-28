// The scoped heater ISR pause used around flash writes: it must end on every return path.
// (3.3.0 set skipHeaterISR by hand and left it true after serving a page: the heater froze.)
#include <unity.h>

#include "ISR.h"

bool skipHeaterISR = false;

void setUp() { skipHeaterISR = false; }
void tearDown() {}

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
            HeaterIsrPause inner;  // e.g. a commit inside a handler that already paused
            TEST_ASSERT_TRUE(skipHeaterISR);
        }
        TEST_ASSERT_TRUE_MESSAGE(skipHeaterISR, "inner pause re-enabled the ISR too early");
    }
    TEST_ASSERT_FALSE(skipHeaterISR);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_isr_pause_ends_on_every_return_path);
    RUN_TEST(test_isr_pause_is_active_inside_scope_and_nests);
    return UNITY_END();
}
