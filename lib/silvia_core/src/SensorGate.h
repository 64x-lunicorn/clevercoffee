/**
 * @file SensorGate.h
 * @brief Decides which sensor readings the controller may use.
 *
 * - A reading is implausible outside 0..150 °C (the TSIC306 driver reports 221/222 on bus
 *   errors) or if it jumps more than 10 °C from the last accepted value.
 * - The very first reading must be plausible as well; nothing seeds the controller until one is.
 * - 10 bad readings in a row (4 s), or no reading for 5 s, raise a sensor error.
 * - The error clears after 25 consistent readings in a row (10 s): plausible and each within
 *   2 °C of the one before. 3.3.0 compared against a stale value instead and latched forever.
 */
#pragma once

#include <stdint.h>

namespace silvia {

class SensorGate {
  public:
    static constexpr float kMin = 0;
    static constexpr float kMax = 150;
    static constexpr float kMaxJump = 10;
    static constexpr int kErrorAfter = 10;
    static constexpr int kClearAfter = 25;
    static constexpr uint32_t kTimeoutMs = 5000;

    void begin(uint32_t nowMs) { lastReadingMs = nowMs; }

    /**
     * @brief feed one reading (already offset-corrected)
     * @return true if the reading was accepted as the current temperature
     */
    bool add(uint32_t nowMs, float celsius);

    /** call every tick, raises the error if readings stopped coming */
    void checkTimeout(uint32_t nowMs);

    bool hasValue() const { return seeded; }
    float value() const { return accepted; }
    bool error() const { return inError; }

  private:
    static bool plausible(float c) { return c == c && c >= kMin && c <= kMax; }

    bool seeded = false;
    bool inError = false;
    float accepted = 0;
    float lastRaw = 0;
    bool lastRawPlausible = false;
    int bad = 0;
    int good = 0;
    uint32_t lastReadingMs = 0;
};

}  // namespace silvia
