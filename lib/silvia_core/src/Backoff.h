/**
 * @file Backoff.h
 * @brief Reconnect timing for WiFi and MQTT: retry forever, 5 s doubling up to 60 s, never
 *        blocking. 3.3.0 gave up on WiFi after ~70 s at boot until the next power cycle.
 */
#pragma once

#include <stdint.h>

namespace silvia {

class Backoff {
  public:
    explicit Backoff(uint32_t firstMs = 5000, uint32_t maxMs = 60000) : first(firstMs), max(maxMs), delay(firstMs) {}

    /** @return true if an attempt should be made now (then the next one is scheduled) */
    bool due(uint32_t nowMs) {
        if (started && (int32_t)(nowMs - nextMs) < 0) return false;
        started = true;
        nextMs = nowMs + delay;
        delay = delay * 2 > max ? max : delay * 2;
        attempts++;
        return true;
    }

    /** call while connected */
    void reset() {
        delay = first;
        started = false;
        attempts = 0;
    }

    uint32_t attemptCount() const { return attempts; }

  private:
    uint32_t first;
    uint32_t max;
    uint32_t delay;
    uint32_t nextMs = 0;
    uint32_t attempts = 0;
    bool started = false;
};

}  // namespace silvia
