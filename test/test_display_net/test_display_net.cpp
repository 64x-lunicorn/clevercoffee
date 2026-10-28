// Display decisions (DisplayModel) and reconnect timing (Backoff).
#include <unity.h>

#include <cmath>

#include "Backoff.h"
#include "DisplayModel.h"

using namespace silvia;

void setUp() {}
void tearDown() {}

namespace {

Snapshot base(State state) {
    Snapshot s = {};
    s.state = state;
    s.temperature = 95.8f;
    s.setpoint = 96;
    return s;
}

}  // namespace

void test_timer_levels() {
    TEST_ASSERT_EQUAL(TimerLevel::Neutral, DisplayModel::levelFor(24.9f));
    TEST_ASSERT_EQUAL(TimerLevel::Highlight, DisplayModel::levelFor(25));
    TEST_ASSERT_EQUAL(TimerLevel::Highlight, DisplayModel::levelFor(30));
    TEST_ASSERT_EQUAL(TimerLevel::TooLong, DisplayModel::levelFor(30.1f));
}

void test_normal_screen_shows_state_in_words() {
    DisplayModel m;
    Screen screen = m.update(base(State::Heating), 0, true, false, false);
    TEST_ASSERT_EQUAL(Layout::Normal, screen.layout);
    TEST_ASSERT_EQUAL_STRING("Aufheizen", screen.stateLabel);
    TEST_ASSERT_EQUAL_STRING("Bereit", m.update(base(State::Ready), 0, true, false, false).stateLabel);
    TEST_ASSERT_EQUAL_STRING("Nachschwingen", m.update(base(State::Recovering), 0, true, false, false).stateLabel);
    TEST_ASSERT_EQUAL_STRING("Dampf", m.update(base(State::Steam), 0, true, false, false).stateLabel);
}

void test_shot_timer_and_ten_second_hold() {
    DisplayModel m;
    Snapshot s = base(State::Shot);
    s.shotTimerS = 27;
    Screen screen = m.update(s, 1000, true, false, false);
    TEST_ASSERT_EQUAL(Layout::Shot, screen.layout);
    TEST_ASSERT_EQUAL(TimerLevel::Highlight, screen.timerLevel);
    TEST_ASSERT_FALSE(screen.timerFinal);

    s = base(State::Heating);
    s.hasLastShot = true;
    s.lastShot.durationS = 31.5f;
    screen = m.update(s, 2000, true, false, false);
    TEST_ASSERT_EQUAL(Layout::Shot, screen.layout);
    TEST_ASSERT_TRUE(screen.timerFinal);
    TEST_ASSERT_EQUAL_FLOAT(31.5f, screen.timerS);
    TEST_ASSERT_EQUAL(TimerLevel::TooLong, screen.timerLevel);

    TEST_ASSERT_EQUAL(Layout::Shot, m.update(s, 11999, true, false, false).layout);
    TEST_ASSERT_EQUAL(Layout::Normal, m.update(s, 12000, true, false, false).layout);
    TEST_ASSERT_EQUAL(Layout::Normal, m.update(s, 13000, true, false, false).layout);
}

void test_error_wins_over_shot_hold() {
    DisplayModel m;
    Snapshot s = base(State::Shot);
    m.update(s, 0, true, false, false);
    s = base(State::Error);
    s.error = ErrorKind::OverTemp;
    s.hasLastShot = true;
    Screen screen = m.update(s, 100, true, false, false);
    TEST_ASSERT_EQUAL(Layout::Error, screen.layout);
    TEST_ASSERT_EQUAL(ErrorKind::OverTemp, screen.error);
}

void test_network_loss_is_only_an_icon() {
    DisplayModel m;
    Screen screen = m.update(base(State::Ready), 0, false, true, false);
    TEST_ASSERT_EQUAL(Layout::Normal, screen.layout);
    TEST_ASSERT_TRUE(screen.wifiLost);
    TEST_ASSERT_FALSE(screen.mqttLost);  // follows from the WiFi loss
    screen = m.update(base(State::Ready), 0, true, true, false);
    TEST_ASSERT_TRUE(screen.mqttLost);
    screen = m.update(base(State::Ready), 0, true, false, false);
    TEST_ASSERT_FALSE(screen.mqttLost);  // MQTT not configured
}

void test_backoff_retries_forever_with_growing_delay() {
    Backoff b(5000, 60000);
    uint32_t t = 0;
    TEST_ASSERT_TRUE(b.due(t));
    TEST_ASSERT_FALSE(b.due(t + 4999));
    TEST_ASSERT_TRUE(b.due(t + 5000));
    TEST_ASSERT_FALSE(b.due(t + 14999));
    TEST_ASSERT_TRUE(b.due(t + 15000));  // 10 s later
    t = 15000;
    for (int i = 0; i < 100; i++) {  // never gives up, delay capped at 60 s
        t += 60000;
        TEST_ASSERT_TRUE(b.due(t));
    }
    TEST_ASSERT_EQUAL(103, b.attemptCount());
    b.reset();
    TEST_ASSERT_TRUE(b.due(t + 1));
    TEST_ASSERT_FALSE(b.due(t + 5000));
    TEST_ASSERT_TRUE(b.due(t + 5001));
}

void test_backoff_survives_millis_wraparound() {
    Backoff b(5000, 60000);
    uint32_t t = 0xFFFFF000u;
    TEST_ASSERT_TRUE(b.due(t));
    TEST_ASSERT_FALSE(b.due(t + 4000));  // wrapped past 0
    TEST_ASSERT_TRUE(b.due(t + 5000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_timer_levels);
    RUN_TEST(test_normal_screen_shows_state_in_words);
    RUN_TEST(test_shot_timer_and_ten_second_hold);
    RUN_TEST(test_error_wins_over_shot_hold);
    RUN_TEST(test_network_loss_is_only_an_icon);
    RUN_TEST(test_backoff_retries_forever_with_growing_delay);
    RUN_TEST(test_backoff_survives_millis_wraparound);
    return UNITY_END();
}
