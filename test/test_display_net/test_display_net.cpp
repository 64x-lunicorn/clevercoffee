// Display decisions (DisplayModel) and reconnect timing (Backoff).
#include <unity.h>

#include <cmath>
#include <cstdlib>
#include <initializer_list>

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

void test_cup_shows_the_condition() {
    DisplayModel m;
    TEST_ASSERT_EQUAL(Cup::Ready, m.update(base(State::Ready), 0, true, false, false).cup);
    TEST_ASSERT_EQUAL(Cup::Ready, m.update(base(State::Recovering), 0, true, false, false).cup);
    TEST_ASSERT_EQUAL(Cup::Ready, m.update(base(State::Steam), 0, true, false, false).cup);
    TEST_ASSERT_EQUAL(Cup::Empty, m.update(base(State::Off), 0, true, false, false).cup);

    Snapshot heating = base(State::Heating);
    heating.temperature = 58;  // halfway from 20 to 96
    Screen screen = m.update(heating, 0, true, false, false);
    TEST_ASSERT_EQUAL(Cup::Heating, screen.cup);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, screen.cupFill);
    heating.temperature = 15;
    TEST_ASSERT_EQUAL_FLOAT(0, m.update(heating, 0, true, false, false).cupFill);

    Snapshot booting = base(State::Booting);
    booting.temperature = NAN;
    screen = m.update(booting, 0, true, false, false);
    TEST_ASSERT_EQUAL(Cup::Heating, screen.cup);
    TEST_ASSERT_EQUAL_FLOAT(0, screen.cupFill);
}

void test_cup_fills_with_the_shot_timer_and_is_hidden_on_errors() {
    DisplayModel m;
    Snapshot s = base(State::Shot);
    s.shotTimerS = 15;
    Screen screen = m.update(s, 0, true, false, false);
    TEST_ASSERT_EQUAL(Cup::Filling, screen.cup);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, screen.cupFill);
    s.shotTimerS = 42;
    TEST_ASSERT_EQUAL_FLOAT(1, m.update(s, 100, true, false, false).cupFill);

    Snapshot error = base(State::Error);
    error.error = ErrorKind::Sensor;
    screen = m.update(error, 200, true, false, false);
    TEST_ASSERT_EQUAL(Cup::Hidden, screen.cup);
    TEST_ASSERT_EQUAL(0, screen.shiftX);
    TEST_ASSERT_EQUAL(0, screen.shiftY);
}

void test_animation_depends_on_time_only() {
    DisplayModel a, b;
    for (int i = 0; i < 17; i++) a.update(base(State::Ready), i * 7, true, false, false);  // extra calls
    for (uint32_t t : {0u, 499u, 500u, 1234u, 987654u}) {
        Screen x = a.update(base(State::Ready), t, true, false, false);
        Screen y = b.update(base(State::Ready), t, true, false, false);
        TEST_ASSERT_EQUAL(x.frame, y.frame);
        TEST_ASSERT_TRUE(x.frame < DisplayModel::kFrames);
    }
    TEST_ASSERT_NOT_EQUAL(a.update(base(State::Ready), 0, true, false, false).frame,
                          a.update(base(State::Ready), DisplayModel::kFrameMs, true, false, false).frame);
}

void test_pixel_shift_is_small_and_slow() {
    DisplayModel m;
    int positions = 0;
    int lastX = 99, lastY = 99;
    for (uint32_t t = 0; t < 4 * 3600000u; t += 10000) {  // 4 h in 10 s steps
        Screen screen = m.update(base(State::Ready), t, true, false, false);
        TEST_ASSERT_TRUE(screen.shiftX >= -2 && screen.shiftX <= 2);
        TEST_ASSERT_TRUE(screen.shiftY >= -1 && screen.shiftY <= 1);
        if (screen.shiftX != lastX || screen.shiftY != lastY) {
            if (lastX != 99) {
                TEST_ASSERT_TRUE(abs(screen.shiftX - lastX) <= 1 && abs(screen.shiftY - lastY) <= 1);
            }
            positions++;
            lastX = screen.shiftX;
            lastY = screen.shiftY;
        }
    }
    TEST_ASSERT_EQUAL(4 * 3600000 / DisplayModel::kShiftMs, positions);  // one move per period
    Screen a = m.update(base(State::Ready), 1000, true, false, false);
    Screen b = m.update(base(State::Ready), 1000 + DisplayModel::kShiftMs - 2000, true, false, false);
    TEST_ASSERT_EQUAL(a.shiftX, b.shiftX);  // not per frame
    TEST_ASSERT_EQUAL(a.shiftY, b.shiftY);
}

void test_standby_cup_drifts_on_screen_and_display_goes_off_when_cold() {
    DisplayModel m;
    Snapshot s = base(State::Standby);
    s.temperature = 70;
    int minX = 999, maxX = -999, minY = 999, maxY = -999;
    for (uint32_t t = 0; t < 600000; t += 100) {
        Screen screen = m.update(s, t, true, false, false);
        TEST_ASSERT_EQUAL(Layout::Standby, screen.layout);
        TEST_ASSERT_EQUAL(Cup::Standby, screen.cup);
        TEST_ASSERT_TRUE(screen.dim);
        TEST_ASSERT_TRUE(screen.displayOn);
        TEST_ASSERT_TRUE(screen.cupX >= 0 && screen.cupX + DisplayModel::kStandbyCupW <= 128);
        TEST_ASSERT_TRUE(screen.cupY >= 0 &&
                         screen.cupY + DisplayModel::kStandbyCupH + DisplayModel::kStandbyTextH <= 64);
        minX = screen.cupX < minX ? screen.cupX : minX;
        maxX = screen.cupX > maxX ? screen.cupX : maxX;
        minY = screen.cupY < minY ? screen.cupY : minY;
        maxY = screen.cupY > maxY ? screen.cupY : maxY;
    }
    TEST_ASSERT_EQUAL(0, minX);  // it really travels the whole screen
    TEST_ASSERT_EQUAL(128 - DisplayModel::kStandbyCupW, maxX);
    TEST_ASSERT_EQUAL(0, minY);
    TEST_ASSERT_EQUAL(64 - DisplayModel::kStandbyCupH - DisplayModel::kStandbyTextH, maxY);

    s.temperature = 40;
    TEST_ASSERT_TRUE(m.update(s, 0, true, false, false).displayOn);
    s.temperature = 39.9f;
    TEST_ASSERT_FALSE(m.update(s, 0, true, false, false).displayOn);
    TEST_ASSERT_TRUE(m.update(base(State::Ready), 0, true, false, false).displayOn);
    TEST_ASSERT_FALSE(m.update(base(State::Ready), 0, true, false, false).dim);
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
    RUN_TEST(test_cup_shows_the_condition);
    RUN_TEST(test_cup_fills_with_the_shot_timer_and_is_hidden_on_errors);
    RUN_TEST(test_animation_depends_on_time_only);
    RUN_TEST(test_pixel_shift_is_small_and_slow);
    RUN_TEST(test_standby_cup_drifts_on_screen_and_display_goes_off_when_cold);
    RUN_TEST(test_backoff_retries_forever_with_growing_delay);
    RUN_TEST(test_backoff_survives_millis_wraparound);
    return UNITY_END();
}
