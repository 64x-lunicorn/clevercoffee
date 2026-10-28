#include "BrewDetector.h"

namespace silvia {

namespace {
const float kSame = 0.05f;           // the TSIC306 resolves ~0.1 °C
const int kTopPlateau = 3;           // more equal readings than this at the top = idle before the shot
const uint32_t kMaxLookBackMs = 20000;
}  // namespace

void BrewDetector::reset(float temperature) {
    filter.reset(temperature);
    rateAverage = 0;
    recentNext = 0;
    recentCount = 0;
    isRunning = false;
}

const BrewDetector::Sample &BrewDetector::recentAt(int back) const {
    return recent[(recentNext - 1 - back + kRecent) % kRecent];
}

uint32_t BrewDetector::backdatedStart(float &top) const {
    int best = 0;
    float maxTemp = recentAt(0).temp;
    int plateau = 0;
    uint32_t now = recentAt(0).ms;

    for (int k = 1; k < recentCount; k++) {
        const Sample &s = recentAt(k);
        if (now - s.ms > kMaxLookBackMs) break;
        if (s.temp > maxTemp + kSame) {
            maxTemp = s.temp;
            best = k;
            plateau = 0;
        } else if (s.temp >= maxTemp - kSame) {
            if (++plateau > kTopPlateau) break;
        } else {
            break;  // lower than the top: we are past the rise before the drop
        }
    }

    top = maxTemp;
    return recentAt(best > 0 ? best - 1 : 0).ms;  // first sample below the top
}

bool BrewDetector::add(uint32_t nowMs, float temperature, float setpoint, float sensitivity, bool armed) {
    rateAverage = filter.add(nowMs, temperature);
    recent[recentNext] = {nowMs, temperature};
    recentNext = (recentNext + 1) % kRecent;
    if (recentCount < kRecent) recentCount++;

    if (isRunning) {
        if (temperature < current.minTemp - kSame) {
            current.minTemp = temperature;
            minMs = nowMs;
        }

        if (rateAverage > 0 || nowMs - current.startMs > kMaxShotMs) {
            current.endMs = rateAverage > 0 ? minMs : nowMs;
            isRunning = false;
            return true;
        }
        return false;
    }

    if (armed && sensitivity > 0 &&
        isSoftwareBrewStart(rateAverage, temperature, setpoint, sensitivity)) {
        detectedMs = nowMs;
        float top;
        current.startMs = backdatedStart(top);
        current.startTemp = top;
        current.minTemp = temperature;
        current.endMs = 0;
        minMs = nowMs;
        for (int k = 0; k < recentCount && recentAt(k).ms >= current.startMs; k++) {
            if (recentAt(k).temp < current.minTemp) {
                current.minTemp = recentAt(k).temp;
                minMs = recentAt(k).ms;
            }
        }
        isRunning = true;
        return true;
    }
    return false;
}

}  // namespace silvia
