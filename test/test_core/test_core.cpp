// The Silvia core, driven only through its public interface: time and sensor readings in,
// heater duty, state snapshot and events out.
#include <Arduino.h>
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "Json.h"
#include "SilviaCore.h"
#include "../test_brew_detection/recorded_shots.h"

using namespace silvia;

namespace {

struct FakeStore : ParamStore {
    std::map<int, double> slots;
    int saves = 0;
    int commits = 0;

    bool load(StorageSlot slot, bool, double &value) override {
        auto it = slots.find(slot);
        if (it == slots.end()) return false;
        value = it->second;
        return true;
    }
    void save(StorageSlot slot, bool, double value) override {
        slots[slot] = value;
        saves++;
    }
    bool commit() override {
        commits++;
        return true;
    }
};

const uint32_t kSampleMs = 400;

struct Rig {
    FakeStore store;
    SilviaCore core{&store};
    uint32_t now = 1000;
    uint16_t duty = 0;
    std::vector<Event> events;

    explicit Rig(bool start = true) {
        if (start) begin();
    }

    void begin() {
        fakeMillis = now;
        core.begin(now);
    }

    // one 400 ms sensor period
    void feed(float raw) {
        now += kSampleMs;
        fakeMillis = now;
        duty = core.tick({now, true, raw, 0});
        drain();
    }

    void silence(uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += kSampleMs) {
            now += kSampleMs;
            fakeMillis = now;
            duty = core.tick({now, false, 0, 0});
            drain();
        }
    }

    void hold(float raw, float seconds) {
        for (int i = 0; i < seconds * 1000 / kSampleMs; i++) feed(raw);
    }

    // walk the temperature in plausible steps, ending exactly at `to`
    void rampTo(float from, float to, float step = 0.5f) {
        float t = from;
        while (fabsf(to - t) > 1e-4f) {
            float d = to - t;
            t = fabsf(d) <= step ? to : t + (d > 0 ? step : -step);
            feed(t);
        }
    }

    void drain() {
        Event e;
        while (core.popEvent(e)) events.push_back(e);
    }

    int count(EventType type) const {
        int n = 0;
        for (const Event &e : events) n += e.type == type;
        return n;
    }

    const Snapshot &s() const { return core.snapshot(); }

    ParamError set(const char *key, const char *value) { return core.setParam(key, value); }
};

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- sensor gate

void test_first_reading_222_at_boot_never_heats_and_raises_sensor_error() {
    Rig rig;
    rig.feed(222);
    TEST_ASSERT_EQUAL(0, rig.duty);
    TEST_ASSERT_TRUE(std::isnan(rig.s().temperature));
    TEST_ASSERT_EQUAL(State::Booting, rig.s().state);

    rig.hold(222, 4);
    TEST_ASSERT_EQUAL(0, rig.duty);
    TEST_ASSERT_EQUAL(State::Error, rig.s().state);
    TEST_ASSERT_EQUAL(ErrorKind::Sensor, rig.s().error);
    TEST_ASSERT_EQUAL(1, rig.count(EventType::ErrorRaised));
}

void test_bad_first_reading_does_not_seed_the_controller() {
    Rig rig;
    rig.feed(222);
    rig.feed(22.5);  // the real cold boiler
    TEST_ASSERT_EQUAL_FLOAT(22.5, rig.s().temperature);
    TEST_ASSERT_EQUAL(State::Heating, rig.s().state);
    rig.hold(23, 10);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    TEST_ASSERT_EQUAL(1000, rig.duty);
}

void test_short_glitches_are_ignored() {
    Rig rig;
    rig.hold(95, 2);
    for (int i = 0; i < 9; i++) rig.feed(221);
    rig.feed(95.1);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    TEST_ASSERT_EQUAL_FLOAT(95.1, rig.s().temperature);
    rig.feed(130);  // plausible range but an impossible jump
    TEST_ASSERT_EQUAL_FLOAT(95.1, rig.s().temperature);
}

void test_sensor_error_latches_until_25_consistent_readings() {
    Rig rig;
    rig.hold(95, 2);
    for (int i = 0; i < 10; i++) rig.feed(222);
    TEST_ASSERT_EQUAL(ErrorKind::Sensor, rig.s().error);
    TEST_ASSERT_EQUAL(0, rig.duty);

    for (int i = 0; i < 24; i++) rig.feed(80 + (i % 2) * 0.1f);  // cooled down meanwhile
    TEST_ASSERT_EQUAL(ErrorKind::Sensor, rig.s().error);
    TEST_ASSERT_EQUAL(0, rig.duty);

    rig.feed(80);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    TEST_ASSERT_EQUAL_FLOAT(80, rig.s().temperature);  // re-seeded, not stuck at 95
    TEST_ASSERT_EQUAL(State::Heating, rig.s().state);
    TEST_ASSERT_EQUAL(1, rig.count(EventType::ErrorCleared));
}

void test_inconsistent_readings_do_not_clear_the_error() {
    Rig rig;
    rig.hold(95, 2);
    for (int i = 0; i < 10; i++) rig.feed(222);
    for (int i = 0; i < 60; i++) rig.feed(i % 2 ? 60 : 95);
    TEST_ASSERT_EQUAL(ErrorKind::Sensor, rig.s().error);
}

void test_missing_readings_raise_sensor_error() {
    Rig rig;
    rig.hold(95, 2);
    rig.silence(4800);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    rig.silence(800);
    TEST_ASSERT_EQUAL(ErrorKind::Sensor, rig.s().error);
    TEST_ASSERT_EQUAL(0, rig.duty);
}

// ---------------------------------------------------------------- emergency stop

static void assertOverTempStops(Rig &rig, float from, float limit) {
    rig.rampTo(from, limit);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    rig.feed(limit + 0.5f);
    TEST_ASSERT_EQUAL(ErrorKind::OverTemp, rig.s().error);
    TEST_ASSERT_EQUAL(State::Error, rig.s().state);
    TEST_ASSERT_EQUAL(0, rig.duty);
}

void test_overtemp_while_heating_up() {
    Rig rig;
    rig.feed(25);
    TEST_ASSERT_EQUAL(State::Heating, rig.s().state);
    assertOverTempStops(rig, 25, 120);
}

void test_overtemp_when_ready_and_hysteresis() {
    Rig rig;
    rig.hold(96, 3);
    TEST_ASSERT_EQUAL(State::Ready, rig.s().state);
    assertOverTempStops(rig, 96, 120);

    rig.rampTo(120.5f, 101.5f);  // still >= brew set point + 5
    TEST_ASSERT_EQUAL(ErrorKind::OverTemp, rig.s().error);
    TEST_ASSERT_EQUAL(0, rig.duty);
    rig.rampTo(101.5f, 100.5f, 0.02f);  // natural cooling, not a shot
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);
    TEST_ASSERT_EQUAL(State::Recovering, rig.s().state);
}

void test_overtemp_with_pid_off() {
    Rig rig;
    rig.hold(96, 1);
    TEST_ASSERT_EQUAL(ParamError::None, rig.set("pid_on", "0"));
    rig.feed(96);
    TEST_ASSERT_EQUAL(State::Off, rig.s().state);
    assertOverTempStops(rig, 96, 120);  // error wins over "off"
}

void test_overtemp_limit_in_steam_and_cool_down() {
    Rig rig;
    rig.hold(96, 1);
    rig.set("steam_on", "1");
    rig.feed(96);
    TEST_ASSERT_EQUAL(State::Steam, rig.s().state);
    rig.rampTo(96, 130);
    TEST_ASSERT_EQUAL(ErrorKind::None, rig.s().error);

    rig.set("steam_on", "0");
    rig.feed(130);
    TEST_ASSERT_EQUAL(State::Recovering, rig.s().state);  // cool-down
    assertOverTempStops(rig, 130, 145);
}

void test_overtemp_during_a_shot() {
    Rig rig;
    rig.hold(95, 30);
    rig.rampTo(95, 92, 0.3f);  // a fast drop starts a shot
    rig.rampTo(92, 88, 0.2f);
    TEST_ASSERT_EQUAL(State::Shot, rig.s().state);
    assertOverTempStops(rig, 88, 120);
}

// ---------------------------------------------------------------- states and control

void test_heat_up_ready_overshoot() {
    Rig rig;
    rig.feed(25);
    TEST_ASSERT_EQUAL(1000, rig.duty);
    rig.rampTo(25, 95.4f);
    TEST_ASSERT_EQUAL(State::Heating, rig.s().state);
    rig.feed(95.5f);
    TEST_ASSERT_EQUAL(State::Ready, rig.s().state);
    TEST_ASSERT_TRUE(rig.s().ready);
    TEST_ASSERT_EQUAL(1, rig.count(EventType::ReadyReached));
    TEST_ASSERT_TRUE(rig.events.back().firstSinceBoot);

    rig.rampTo(95.5f, 96.9f, 0.1f);  // within the hold band
    TEST_ASSERT_EQUAL(State::Ready, rig.s().state);
    rig.rampTo(96.9f, 97.2f, 0.1f);
    TEST_ASSERT_EQUAL(State::Recovering, rig.s().state);
    rig.rampTo(97.2f, 96.3f, 0.1f);
    TEST_ASSERT_EQUAL(State::Ready, rig.s().state);
    TEST_ASSERT_EQUAL(2, rig.count(EventType::ReadyReached));
    TEST_ASSERT_FALSE(rig.events.back().firstSinceBoot);
}

void test_pid_off_switches_heater_off_and_back_on() {
    Rig rig;
    rig.feed(60);
    TEST_ASSERT_EQUAL(1000, rig.duty);
    rig.set("pid_on", "false");
    rig.feed(60);
    TEST_ASSERT_EQUAL(0, rig.duty);
    TEST_ASSERT_EQUAL(State::Off, rig.s().state);
    rig.set("pid_on", "ON");
    rig.hold(60, 1.2f);  // the PID computes once per second
    TEST_ASSERT_EQUAL(1000, rig.duty);
}

void test_steam_uses_steam_setpoint_and_cool_down_ends_with_zero_brew_pid() {
    Rig rig;
    rig.set("bd_kp", "0");
    rig.set("bd_tn", "0");
    rig.set("bd_tv", "0");
    rig.hold(96, 2);
    rig.set("steam_on", "1");
    rig.hold(96, 1.2f);
    TEST_ASSERT_EQUAL_FLOAT(120, rig.s().setpoint);
    TEST_ASSERT_EQUAL(1000, rig.duty);  // Kp 150 * 24 °C
    rig.rampTo(96, 121);
    TEST_ASSERT_EQUAL(0, rig.duty);

    rig.set("steam_on", "0");
    rig.feed(121);
    TEST_ASSERT_EQUAL(State::Recovering, rig.s().state);
    TEST_ASSERT_EQUAL(0, rig.duty);
    // the boiler cools steadily, the rate never turns positive: 3.3.0 stayed in cool-down
    rig.rampTo(121, 98.1f, 0.02f);
    TEST_ASSERT_EQUAL(State::Recovering, rig.s().state);
    rig.rampTo(98.1f, 96.4f, 0.02f);
    TEST_ASSERT_EQUAL(State::Ready, rig.s().state);
    TEST_ASSERT_EQUAL_FLOAT(96, rig.s().setpoint);
}

void test_offset_is_applied_in_every_mode() {
    Rig rig;
    rig.set("temp_offset", "2");
    rig.hold(98, 2);
    TEST_ASSERT_EQUAL_FLOAT(96, rig.s().temperature);
    rig.set("steam_on", "1");
    rig.feed(98);
    TEST_ASSERT_EQUAL_FLOAT(96, rig.s().temperature);  // 3.3.0 dropped the offset in steam mode
    rig.set("steam_on", "0");
    rig.feed(98);
    TEST_ASSERT_EQUAL_FLOAT(96, rig.s().temperature);
}

// ---------------------------------------------------------------- shots

struct ReplayResult {
    std::vector<Event> started;
    std::vector<Event> finished;
    uint32_t base;
    float timerAtDetection;
};

static ReplayResult replay(const RecordedShot &shot, const char *sensitivity = "120") {
    Rig rig(false);
    rig.store.slots[kSlotBrewSetpoint] = 95;  // the set point when the shots were recorded
    rig.store.slots[kSlotBdThreshold] = atof(sensitivity);
    rig.begin();
    ReplayResult r;
    r.base = rig.now + kSampleMs;  // time of sample 0
    r.timerAtDetection = -1;
    for (int i = 0; i < shot.count; i++) {
        size_t before = rig.events.size();
        rig.feed(shot.tenths[i] / 10.0f);
        for (size_t k = before; k < rig.events.size(); k++) {
            if (rig.events[k].type == EventType::ShotStarted) {
                r.started.push_back(rig.events[k]);
                r.timerAtDetection = rig.s().shotTimerS;
            }
            if (rig.events[k].type == EventType::ShotFinished) r.finished.push_back(rig.events[k]);
        }
    }
    return r;
}

void test_recorded_shots_yield_one_shot_each_with_backdated_start_and_end() {
    char msg[128];
    for (const RecordedShot &shot : recordedShots) {
        ReplayResult r = replay(shot);
        snprintf(msg, sizeof msg, "%s: shots", shot.name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(shot.drawCount, (int)r.started.size(), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(shot.drawCount, (int)r.finished.size(), msg);

        for (int d = 0; d < shot.drawCount; d++) {
            const ShotSummary &s = r.finished[d].shot;
            long start = (long)(s.startMs - r.base);
            float duration = (shot.expectedEndMs[d] - shot.expectedStartMs[d]) / 1000.0f;
            snprintf(msg, sizeof msg, "%s draw %d: start %ld ms, expected %d ms", shot.name, d, start,
                     shot.expectedStartMs[d]);
            TEST_ASSERT_INT_WITHIN_MESSAGE(1000, shot.expectedStartMs[d], start, msg);
            snprintf(msg, sizeof msg, "%s draw %d: duration %.1f s, expected %.1f s", shot.name, d, s.durationS,
                     duration);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, duration, s.durationS, msg);
            TEST_ASSERT_TRUE_MESSAGE(s.minTemp < s.startTemp - 6, msg);
        }
    }
}

void test_shot3_lasts_33_seconds() {
    ReplayResult r = replay(recordedShots[2]);
    TEST_ASSERT_EQUAL(1, r.finished.size());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 33.0f, r.finished[0].shot.durationS);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 88.1f, r.finished[0].shot.minTemp);
}

void test_shot_timer_is_backdated_at_detection() {
    // detection fires ~4 s into the shot; the timer must already show that
    ReplayResult r = replay(recordedShots[2]);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 4.0f, r.timerAtDetection);
}

void test_detection_off_with_sensitivity_zero() {
    ReplayResult r = replay(recordedShots[2], "0");
    TEST_ASSERT_EQUAL(0, r.started.size());
}

void test_slow_cooling_is_not_a_shot() {
    Rig rig;
    rig.hold(96, 30);
    // hot-water draw: ~0.03 °C/s for 4 minutes
    for (int i = 0; i < 600; i++) rig.feed(96 - 0.03f * i * 0.4f);
    TEST_ASSERT_EQUAL(0, rig.count(EventType::ShotStarted));
}

void test_shot_states_and_summary() {
    Rig rig;
    rig.hold(96, 30);
    rig.rampTo(96, 92, 0.3f);
    TEST_ASSERT_EQUAL(State::Shot, rig.s().state);
    TEST_ASSERT_FALSE(rig.s().ready);
    rig.rampTo(92, 89, 0.1f);
    rig.hold(89, 2);
    rig.rampTo(89, 94, 0.1f);  // recovering
    TEST_ASSERT_EQUAL(1, rig.count(EventType::ShotFinished));
    TEST_ASSERT_EQUAL(State::Heating, rig.s().state);
    TEST_ASSERT_EQUAL(1, rig.s().shotCount);
    TEST_ASSERT_TRUE(rig.s().hasLastShot);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 89, rig.s().lastShot.minTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 96, rig.s().lastShot.startTemp);

    rig.rampTo(94, 96.8f, 0.1f);  // ready again, then overshoot
    TEST_ASSERT_FALSE(std::isnan(rig.s().lastShot.readyAfterS));
    rig.rampTo(96.8f, 96, 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 96.8f, rig.s().lastShot.peakTemp);
    TEST_ASSERT_FALSE(rig.s().lastShot.complete);
    rig.hold(96, 300);
    TEST_ASSERT_TRUE(rig.s().lastShot.complete);
    TEST_ASSERT_EQUAL(1, rig.count(EventType::ShotComplete));
}

void test_brew_pid_off_by_default_and_switches_immediately_when_on() {
    Rig off;
    off.hold(96, 30);
    off.rampTo(96, 92, 0.3f);
    TEST_ASSERT_FALSE(off.s().brewPidActive);

    Rig on;
    on.set("bd_pid_on", "1");
    on.set("bd_time", "30");
    on.hold(96, 30);
    on.rampTo(96, 92, 0.3f);
    TEST_ASSERT_EQUAL(State::Shot, on.s().state);
    TEST_ASSERT_TRUE(on.s().brewPidActive);
    on.hold(92, 31);
    TEST_ASSERT_FALSE(on.s().brewPidActive);
}

// ---------------------------------------------------------------- parameters

void test_defaults() {
    Rig rig;
    const ParamSet &p = rig.core.params();
    TEST_ASSERT_EQUAL_DOUBLE(96, p.get(kBrewSetpoint));
    TEST_ASSERT_EQUAL_DOUBLE(62, p.get(kPidKp));
    TEST_ASSERT_EQUAL_DOUBLE(52, p.get(kPidTn));
    TEST_ASSERT_EQUAL_FLOAT(11.5, p.get(kPidTv));
    TEST_ASSERT_EQUAL_DOUBLE(75, p.get(kPidIMax));
    TEST_ASSERT_EQUAL_DOUBLE(120, p.get(kSteamSetpoint));
    TEST_ASSERT_EQUAL_DOUBLE(120, p.get(kBdSensitivity));
    TEST_ASSERT_EQUAL_DOUBLE(30, p.get(kBdTime));
    TEST_ASSERT_FALSE(p.flag(kBdPidOn));
    TEST_ASSERT_TRUE(p.flag(kPidOn));
}

void test_stored_values_win_and_bad_ones_fall_back_to_defaults() {
    Rig rig(false);
    rig.store.slots[kSlotBrewSetpoint] = 95.5;
    rig.store.slots[kSlotPidKpRegular] = 80;
    rig.store.slots[kSlotPidOn] = 0;
    rig.store.slots[kSlotPidTvRegular] = 5000;  // out of range
    rig.store.slots[kSlotPidKpBd] = NAN;
    rig.store.slots[kSlotUseBdPid] = 1;
    rig.begin();
    const ParamSet &p = rig.core.params();
    TEST_ASSERT_EQUAL_DOUBLE(95.5, p.get(kBrewSetpoint));
    TEST_ASSERT_EQUAL_DOUBLE(80, p.get(kPidKp));
    TEST_ASSERT_FALSE(p.flag(kPidOn));
    TEST_ASSERT_EQUAL_FLOAT(11.5, p.get(kPidTv));
    TEST_ASSERT_EQUAL_DOUBLE(50, p.get(kBdKp));
    TEST_ASSERT_TRUE(p.flag(kBdPidOn));
    TEST_ASSERT_FALSE(p.flag(kSteamOn));  // never persisted
}

void test_setpoint_150_is_rejected_and_stays_96() {
    Rig rig;
    TEST_ASSERT_EQUAL(ParamError::OutOfRange, rig.set("brew_setpoint", "150"));
    TEST_ASSERT_EQUAL_DOUBLE(96, rig.core.params().get(kBrewSetpoint));
    TEST_ASSERT_EQUAL(0, rig.store.saves);
}

void test_every_parameter_validates_its_limits() {
    char text[32];
    for (int i = 0; i < kParamCount; i++) {
        const ParamDef &d = paramDef((ParamId)i);
        Rig rig;
        snprintf(text, sizeof text, "%g", d.max);
        TEST_ASSERT_EQUAL_MESSAGE(ParamError::None, rig.set(d.key, text), d.key);
        TEST_ASSERT_EQUAL_DOUBLE_MESSAGE(d.max, rig.core.params().get((ParamId)i), d.key);
        snprintf(text, sizeof text, "%g", d.min);
        TEST_ASSERT_EQUAL_MESSAGE(ParamError::None, rig.set(d.key, text), d.key);
        ParamError outside = d.type == ParamType::Bool ? ParamError::Invalid : ParamError::OutOfRange;
        snprintf(text, sizeof text, "%g", d.max + 1);
        TEST_ASSERT_EQUAL_MESSAGE(outside, rig.set(d.key, text), d.key);
        snprintf(text, sizeof text, "%g", d.min - 1);
        TEST_ASSERT_EQUAL_MESSAGE(outside, rig.set(d.key, text), d.key);
        TEST_ASSERT_EQUAL_MESSAGE(ParamError::Invalid, rig.set(d.key, ""), d.key);
        TEST_ASSERT_EQUAL_MESSAGE(ParamError::Invalid, rig.set(d.key, "12abc"), d.key);
        TEST_ASSERT_EQUAL_MESSAGE(ParamError::Invalid, rig.set(d.key, "nan"), d.key);
        TEST_ASSERT_EQUAL_DOUBLE_MESSAGE(d.min, rig.core.params().get((ParamId)i), d.key);
        if (d.type == ParamType::Bool) {
            TEST_ASSERT_EQUAL_MESSAGE(ParamError::Invalid, rig.set(d.key, "0.5"), d.key);
        }
    }
}

void test_read_only_and_unknown_keys_are_rejected() {
    Rig rig;
    rig.hold(95, 1);
    TEST_ASSERT_EQUAL(ParamError::ReadOnly, rig.set("TEMP", "20"));
    TEST_ASSERT_EQUAL(ParamError::ReadOnly, rig.set("temperature", "20"));
    TEST_ASSERT_EQUAL(ParamError::ReadOnly, rig.set("version", "x"));
    TEST_ASSERT_EQUAL(ParamError::ReadOnly, rig.set("state", "ready"));
    TEST_ASSERT_EQUAL(ParamError::Unknown, rig.set("BACKFLUSH_ON", "1"));
    rig.feed(95);
    TEST_ASSERT_EQUAL_FLOAT(95, rig.s().temperature);
}

void test_batch_is_all_or_nothing_and_saved_once() {
    Rig rig;
    const char *keys[] = {"brew_setpoint", "pid_kp", "TEMP"};
    const char *vals[] = {"95", "70", "93.2"};
    FieldError errors[4];
    TEST_ASSERT_EQUAL(1, rig.core.setParams(keys, vals, 3, errors, 4));
    TEST_ASSERT_EQUAL_STRING("TEMP", errors[0].key);
    TEST_ASSERT_EQUAL(ParamError::ReadOnly, errors[0].error);
    TEST_ASSERT_EQUAL_DOUBLE(96, rig.core.params().get(kBrewSetpoint));

    TEST_ASSERT_EQUAL(0, rig.core.setParams(keys, vals, 2, errors, 4));
    TEST_ASSERT_EQUAL_DOUBLE(95, rig.core.params().get(kBrewSetpoint));
    TEST_ASSERT_EQUAL_DOUBLE(70, rig.core.params().get(kPidKp));
    TEST_ASSERT_EQUAL(2, rig.store.saves);
    TEST_ASSERT_EQUAL(1, rig.store.commits);
}

void test_unchanged_values_are_not_written_to_flash() {
    Rig rig;
    rig.set("brew_setpoint", "96");
    rig.set("steam_on", "1");  // runtime only
    TEST_ASSERT_EQUAL(0, rig.store.saves);
    TEST_ASSERT_EQUAL(0, rig.store.commits);
    rig.set("brew_setpoint", "96.5");
    TEST_ASSERT_EQUAL(1, rig.store.commits);
    TEST_ASSERT_EQUAL_DOUBLE(96.5, rig.store.slots[kSlotBrewSetpoint]);
}

void test_legacy_names_are_accepted() {
    Rig rig;
    TEST_ASSERT_EQUAL(ParamError::None, rig.set("BREW_SET_POINT", "95"));
    TEST_ASSERT_EQUAL(ParamError::None, rig.set("PID_BD_TN", "500"));  // 3.3.0 limited this to 200
    TEST_ASSERT_EQUAL_DOUBLE(500, rig.core.params().get(kBdTn));
}

// ---------------------------------------------------------------- JSON

void test_state_document() {
    Snapshot s = {};
    s.uptimeMs = 123456;
    s.temperature = 95.2f;
    s.setpoint = s.brewSetpoint = 96;
    s.steamSetpoint = 120;
    s.heaterDuty = 275;
    s.state = State::Ready;
    s.ready = true;
    s.pidOn = true;
    s.shotCount = 2;
    s.hasLastShot = true;
    s.lastShot = {2, 100000, 0, 32.7f, 95.1f, 88.1f, NAN, NAN, false};
    DeviceInfo device = {"test", -60, true, false, 30000};

    char buf[1024];
    JsonOut out(buf, sizeof buf);
    writeState(out, s, device);
    TEST_ASSERT_FALSE(out.overflow());
    TEST_ASSERT_EQUAL_STRING(
        "{\"temperature\":95.2,\"setpoint\":96.0,\"brew_setpoint\":96.0,\"steam_setpoint\":120.0,"
        "\"heater\":27.5,\"state\":\"ready\",\"state_label\":\"Bereit\",\"ready\":true,\"error\":\"none\","
        "\"pid_on\":true,\"steam_on\":false,\"brew_pid_active\":false,\"shot_timer_s\":null,\"shot_count\":2,"
        "\"last_shot\":{\"id\":2,\"start_uptime_s\":100.0,\"start_unix\":null,\"duration_s\":32.7,"
        "\"start_temp\":95.1,\"min_temp\":88.1,\"peak_temp\":null,\"ready_after_s\":null,\"complete\":false},"
        "\"uptime_s\":123,\"wifi_rssi\":-60,\"mqtt_connected\":false,\"free_heap\":30000,\"version\":\"test\"}",
        buf);
}

void test_parameters_and_errors_documents() {
    Rig rig;
    static char buf[8192];
    JsonOut out(buf, sizeof buf);
    writeParameters(out, rig.core.params());
    TEST_ASSERT_FALSE(out.overflow());
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"key\":\"brew_setpoint\",\"label\":\"Brühtemperatur\","));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"value\":96.00,\"min\":80.0,\"max\":105.0"));

    FieldError errors[] = {{"brew_setpoint", ParamError::OutOfRange}, {"TEMP", ParamError::ReadOnly}};
    JsonOut err(buf, sizeof buf);
    writeFieldErrors(err, errors, 2);
    TEST_ASSERT_EQUAL_STRING("{\"errors\":{\"brew_setpoint\":{\"code\":\"out_of_range\",\"min\":80.0,\"max\":105.0},"
                             "\"TEMP\":{\"code\":\"read_only\"}}}",
                             buf);
}

void test_json_escapes_and_reports_overflow() {
    char buf[16];
    JsonOut out(buf, sizeof buf);
    out.beginObject().key("a").str("x\"y\n").endObject();
    TEST_ASSERT_EQUAL_STRING("{\"a\":\"x\\\"y\\n\"}", buf);
    JsonOut tiny(buf, 8);
    tiny.beginObject().key("long").str("value").endObject();
    TEST_ASSERT_TRUE(tiny.overflow());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_reading_222_at_boot_never_heats_and_raises_sensor_error);
    RUN_TEST(test_bad_first_reading_does_not_seed_the_controller);
    RUN_TEST(test_short_glitches_are_ignored);
    RUN_TEST(test_sensor_error_latches_until_25_consistent_readings);
    RUN_TEST(test_inconsistent_readings_do_not_clear_the_error);
    RUN_TEST(test_missing_readings_raise_sensor_error);
    RUN_TEST(test_overtemp_while_heating_up);
    RUN_TEST(test_overtemp_when_ready_and_hysteresis);
    RUN_TEST(test_overtemp_with_pid_off);
    RUN_TEST(test_overtemp_limit_in_steam_and_cool_down);
    RUN_TEST(test_overtemp_during_a_shot);
    RUN_TEST(test_heat_up_ready_overshoot);
    RUN_TEST(test_pid_off_switches_heater_off_and_back_on);
    RUN_TEST(test_steam_uses_steam_setpoint_and_cool_down_ends_with_zero_brew_pid);
    RUN_TEST(test_offset_is_applied_in_every_mode);
    RUN_TEST(test_recorded_shots_yield_one_shot_each_with_backdated_start_and_end);
    RUN_TEST(test_shot3_lasts_33_seconds);
    RUN_TEST(test_shot_timer_is_backdated_at_detection);
    RUN_TEST(test_detection_off_with_sensitivity_zero);
    RUN_TEST(test_slow_cooling_is_not_a_shot);
    RUN_TEST(test_shot_states_and_summary);
    RUN_TEST(test_brew_pid_off_by_default_and_switches_immediately_when_on);
    RUN_TEST(test_defaults);
    RUN_TEST(test_stored_values_win_and_bad_ones_fall_back_to_defaults);
    RUN_TEST(test_setpoint_150_is_rejected_and_stays_96);
    RUN_TEST(test_every_parameter_validates_its_limits);
    RUN_TEST(test_read_only_and_unknown_keys_are_rejected);
    RUN_TEST(test_batch_is_all_or_nothing_and_saved_once);
    RUN_TEST(test_unchanged_values_are_not_written_to_flash);
    RUN_TEST(test_legacy_names_are_accepted);
    RUN_TEST(test_state_document);
    RUN_TEST(test_parameters_and_errors_documents);
    RUN_TEST(test_json_escapes_and_reports_overflow);
    return UNITY_END();
}
