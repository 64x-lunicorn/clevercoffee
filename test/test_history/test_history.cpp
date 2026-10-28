// On-device data: 15 min history at 1 s, last 10 shots, last shot curve, and their JSON.
#include <Arduino.h>
#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>

#include "History.h"
#include "HistoryJson.h"
#include "Json.h"
#include "SilviaCore.h"
#include "../test_brew_detection/recorded_shots.h"

using namespace silvia;

namespace {

// ticks every 100 ms like the firmware, sensor reading every 400 ms
struct Rig {
    SilviaCore core;
    History history;
    uint32_t now = 1000;
    int tick = 0;

    Rig() {
        fakeMillis = now;
        core.begin(now);
    }

    void step(float raw) {
        for (int i = 0; i < 4; i++) {
            now += 100;
            fakeMillis = now;
            core.tick({now, i == 3, raw, 0});
            history.update(core.snapshot());
            Event e;
            while (core.popEvent(e)) history.onEvent(e);
        }
    }

    void hold(float raw, float seconds) {
        for (int i = 0; i < seconds / 0.4f; i++) step(raw);
    }

    // a quick synthetic shot: drop 96 -> 89 within ~14 s, then back up
    void shot() {
        for (float t = 96; t > 89; t -= 0.2f) step(t);
        hold(89, 1);
        for (float t = 89; t < 96; t += 0.05f) step(t);
        hold(96, 20);
    }
};

std::string readAll(HistoryJson &json, size_t chunk) {
    std::string text;
    char buf[512];
    for (size_t n; (n = json.read(buf, chunk)) > 0;) text.append(buf, n);
    return text;
}

int countOf(const std::string &s, char c) {
    int n = 0;
    for (char x : s) n += x == c;
    return n;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_history_keeps_15_minutes_at_1_second() {
    Rig rig;
    rig.hold(96, 20 * 60);
    TEST_ASSERT_EQUAL(900, rig.history.sampleCount());
    uint32_t span = rig.history.lastSampleMs() - 899 * 1000;
    TEST_ASSERT_UINT32_WITHIN(1000, rig.now - 15 * 60 * 1000, span);
    TEST_ASSERT_EQUAL_FLOAT(96, History::temp(rig.history.sample(899)));
    TEST_ASSERT_EQUAL((int)State::Ready, rig.history.sample(899).state);
}

void test_history_wraps_oldest_first() {
    Rig rig;
    rig.hold(60, 400);   // heating
    rig.hold(61, 600);   // 1000 s in total: the 60 °C part is partly overwritten
    TEST_ASSERT_EQUAL_FLOAT(60, History::temp(rig.history.sample(0)));
    TEST_ASSERT_EQUAL_FLOAT(61, History::temp(rig.history.sample(899)));
    int sixty = 0;
    for (int i = 0; i < 900; i++) sixty += History::temp(rig.history.sample(i)) == 60;
    TEST_ASSERT_INT_WITHIN(2, 300, sixty);
    TEST_ASSERT_EQUAL_FLOAT(100, History::heaterPercent(rig.history.sample(0)));
}

void test_unknown_temperature_is_null() {
    Rig rig;
    rig.hold(222, 2);  // no valid reading yet
    TEST_ASSERT_TRUE(std::isnan(History::temp(rig.history.sample(0))));
    HistoryJson json(rig.history);
    std::string text = readAll(json, 64);
    TEST_ASSERT_NOT_NULL(strstr(text.c_str(), "\"temperature\":[null,null"));
}

void test_chunked_document_is_identical_for_any_chunk_size() {
    Rig rig;
    rig.hold(25, 3);
    for (float t = 25; t < 95.5f; t += 0.5f) rig.step(t);
    rig.hold(95.5f, 900);
    HistoryJson a(rig.history), b(rig.history), c(rig.history);
    std::string big = readAll(a, 512);
    TEST_ASSERT_EQUAL_STRING(big.c_str(), readAll(b, 7).c_str());
    TEST_ASSERT_EQUAL_STRING(big.c_str(), readAll(c, 1).c_str());
    TEST_ASSERT_EQUAL('{', big.front());
    TEST_ASSERT_EQUAL('}', big.back());
    TEST_ASSERT_EQUAL(countOf(big, '['), countOf(big, ']'));
    // 2 header fields, the states legend of 8, 4 arrays of 900 values
    TEST_ASSERT_EQUAL(2 + 7 + 4 + 4 * 899, countOf(big, ','));
    TEST_ASSERT_TRUE(big.size() < 16000);
}

void test_small_document_exactly() {
    Rig rig;
    rig.step(95.0f);
    rig.step(95.25f);
    rig.step(95.5f);
    HistoryJson json(rig.history);
    std::string text = readAll(json, 512);
    // one sample per second: 1.2 s of ticks give 2 samples
    TEST_ASSERT_EQUAL_STRING(
        "{\"interval_s\":1,\"end_uptime_s\":2,\"states\":[\"off\",\"booting\",\"heating\",\"ready\",\"shot\","
        "\"recovering\",\"steam\",\"error\"],\"temperature\":[null,95.3],\"setpoint\":[96.0,96.0],"
        "\"heater\":[0.0,6.4],\"state\":[1,2]}",
        text.c_str());
}

void test_last_ten_shots_newest_first() {
    Rig rig;
    rig.hold(96, 30);
    for (int i = 0; i < 12; i++) rig.shot();
    TEST_ASSERT_EQUAL(12, rig.core.snapshot().shotCount);
    TEST_ASSERT_EQUAL(10, rig.history.shotCount());
    TEST_ASSERT_EQUAL(12, rig.history.shot(0).id);
    TEST_ASSERT_EQUAL(3, rig.history.shot(9).id);
}

void test_curve_of_the_last_shot_starts_at_the_backdated_start() {
    Rig rig;
    const RecordedShot &shot = recordedShots[2];
    for (int i = 0; i < shot.count; i++) rig.step(shot.tenths[i] / 10.0f);
    TEST_ASSERT_EQUAL(1, rig.history.curveShotId());
    TEST_ASSERT_EQUAL(History::kCurve, rig.history.curveLength());
    float first = History::temp(rig.history.curveSample(0));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, rig.history.shot(0).startTemp, first);
    float lowest = 200;
    for (int i = 0; i < rig.history.curveLength(); i++) {
        float t = History::temp(rig.history.curveSample(i));
        if (t < lowest) lowest = t;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.15f, rig.history.shot(0).minTemp, lowest);
}

void test_shots_document() {
    Rig rig;
    rig.hold(96, 30);
    rig.shot();
    static char buf[8192];
    JsonOut out(buf, sizeof buf);
    writeShots(out, rig.history);
    TEST_ASSERT_FALSE(out.overflow());
    TEST_ASSERT_EQUAL(0, strncmp(buf, "{\"shots\":[{\"id\":1,", 18));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_curve\":{\"shot_id\":1,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"heater\":["));
}

void test_event_document() {
    Event e = {};
    e.type = EventType::ReadyReached;
    e.atMs = 61500;
    e.firstSinceBoot = true;
    char buf[256];
    JsonOut out(buf, sizeof buf);
    writeEvent(out, e);
    TEST_ASSERT_EQUAL_STRING("{\"event\":\"ready_reached\",\"uptime_s\":61.5,\"first_since_boot\":true}", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_history_keeps_15_minutes_at_1_second);
    RUN_TEST(test_history_wraps_oldest_first);
    RUN_TEST(test_unknown_temperature_is_null);
    RUN_TEST(test_chunked_document_is_identical_for_any_chunk_size);
    RUN_TEST(test_small_document_exactly);
    RUN_TEST(test_last_ten_shots_newest_first);
    RUN_TEST(test_curve_of_the_last_shot_starts_at_the_backdated_start);
    RUN_TEST(test_shots_document);
    RUN_TEST(test_event_document);
    return UNITY_END();
}
