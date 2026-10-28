#include "SensorGate.h"

#include <math.h>

namespace silvia {

bool SensorGate::add(uint32_t nowMs, float celsius) {
    lastReadingMs = nowMs;
    bool ok = plausible(celsius);

    if (inError) {
        bool consistent = ok && lastRawPlausible && fabsf(celsius - lastRaw) <= 2;
        good = consistent ? good + 1 : 0;
        lastRaw = celsius;
        lastRawPlausible = ok;
        if (good + 1 < kClearAfter) return false;  // the first reading of the run counts too
        inError = false;
        bad = 0;
        seeded = true;
        accepted = celsius;
        return true;
    }

    lastRaw = celsius;
    lastRawPlausible = ok;

    if (ok && seeded && fabsf(celsius - accepted) > kMaxJump) ok = false;

    if (!ok) {
        if (++bad >= kErrorAfter) {
            inError = true;
            good = 0;
        }
        return false;
    }

    bad = 0;
    seeded = true;
    accepted = celsius;
    return true;
}

void SensorGate::checkTimeout(uint32_t nowMs) {
    if (!inError && nowMs - lastReadingMs > kTimeoutMs) {
        inError = true;
        good = 0;
        lastRawPlausible = false;
    }
}

}  // namespace silvia
