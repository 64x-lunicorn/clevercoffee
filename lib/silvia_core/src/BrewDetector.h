/**
 * @file BrewDetector.h
 * @brief Software shot detection from the boiler temperature (the Silvia E has no brew switch).
 *
 * Detection is the 3.3.0 logic, unchanged: the averaged temperature change rate
 * (TempRateFilter) falls below -sensitivity while the temperature is within 5 °C of the brew set
 * point. That fires about 4 s after the pump starts, so the start is back-dated:
 *
 * - **Start**: the first sample of the falling edge, found by walking back from the detection
 *   through the recent readings to the top of the drop.
 * - **End**: once the averaged rate turns positive again, the end is the first sample at the
 *   lowest temperature since the start, i.e. the moment the fall stopped (the pump stopped
 *   pushing cold water into the boiler).
 *
 * A shot that never turns around is ended after kMaxShotMs.
 */
#pragma once

#include <stdint.h>

#include "TempRateFilter.h"

namespace silvia {

class BrewDetector {
  public:
    static constexpr int kRecent = 80;               // 32 s of readings at 400 ms
    static constexpr uint32_t kMaxShotMs = 120000;
    static constexpr float kSetpointWindow = 5;      // °C, as in 3.3.0

    struct Shot {
        uint32_t startMs;
        uint32_t endMs;      // valid once ended
        float startTemp;     // top of the drop
        float minTemp;
    };

    void reset(float temperature);

    /**
     * @brief feed one accepted reading
     * @param armed false while detection must not fire (steam, cool-down, PID off)
     * @return true if a shot started or ended with this reading (see running()/shot())
     */
    bool add(uint32_t nowMs, float temperature, float setpoint, float sensitivity, bool armed);

    bool running() const { return isRunning; }
    const Shot &shot() const { return current; }
    /** time of the detection itself (brew PID timing counts from here, as in 3.3.0) */
    uint32_t detectedAtMs() const { return detectedMs; }
    double rate() const { return rateAverage; }

  private:
    struct Sample {
        uint32_t ms;
        float temp;
    };

    const Sample &recentAt(int back) const;  // 0 = newest
    uint32_t backdatedStart(float &top) const;

    TempRateFilter filter;
    double rateAverage = 0;
    Sample recent[kRecent] = {};
    int recentNext = 0;
    int recentCount = 0;

    bool isRunning = false;
    Shot current = {};
    uint32_t detectedMs = 0;
    uint32_t minMs = 0;
};

}  // namespace silvia
