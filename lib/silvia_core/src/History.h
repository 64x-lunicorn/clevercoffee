/**
 * @file History.h
 * @brief On-device data: 15 min at 1 s, the last 10 shot summaries and the 1 s curve of the
 *        most recent shot. Stored compactly (about 6 KB) for the ESP8266's ~40 KB heap.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SilviaCore.h"

namespace silvia {

struct HistorySample {
    int16_t tempTenths;      // kNoTemp if unknown
    int16_t setpointTenths;
    uint8_t heaterQuarter;   // heater duty / 4 (0..250)
    uint8_t state;           // silvia::State
};

class History {
  public:
    static constexpr int kSamples = 900;     // 15 min at 1 s
    static constexpr int kShots = 10;
    static constexpr int kCurve = 120;       // s
    static constexpr int16_t kNoTemp = INT16_MIN;
    static constexpr uint32_t kIntervalMs = 1000;

    /** call after every core tick; records once per second and follows shot events */
    void update(const Snapshot &s);
    void onEvent(const Event &e);

    int sampleCount() const { return count; }
    /** 0 = oldest */
    const HistorySample &sample(int i) const { return samples[(next - count + i + kSamples) % kSamples]; }
    uint32_t lastSampleMs() const { return lastMs; }

    int shotCount() const { return shotsStored; }
    /** 0 = newest */
    const ShotSummary &shot(int i) const { return shots[(shotNext - 1 - i + kShots) % kShots]; }

    uint32_t curveShotId() const { return curveId; }
    uint32_t curveStartMs() const { return curveStart; }
    int curveLength() const { return curveLen; }
    const HistorySample &curveSample(int i) const { return curve[i]; }

    static float temp(const HistorySample &s) { return s.tempTenths == kNoTemp ? NAN_VALUE() : s.tempTenths / 10.0f; }
    static float heaterPercent(const HistorySample &s) { return s.heaterQuarter * 0.4f; }

  private:
    static float NAN_VALUE() { return __builtin_nanf(""); }
    void storeShot(const ShotSummary &shot);
    static HistorySample pack(const Snapshot &s);

    HistorySample samples[kSamples];
    int next = 0;
    int count = 0;
    uint32_t lastMs = 0;
    bool started = false;

    ShotSummary shots[kShots];
    int shotNext = 0;
    int shotsStored = 0;

    HistorySample curve[kCurve];
    int curveLen = 0;
    uint32_t curveId = 0;
    uint32_t curveStart = 0;
    bool curveRecording = false;
};

}  // namespace silvia
