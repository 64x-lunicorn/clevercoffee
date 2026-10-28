#include "History.h"

namespace silvia {

HistorySample History::pack(const Snapshot &s) {
    HistorySample h;
    h.tempTenths = s.temperature == s.temperature ? (int16_t)(s.temperature * 10 + (s.temperature >= 0 ? 0.5f : -0.5f))
                                                  : kNoTemp;
    h.setpointTenths = (int16_t)(s.setpoint * 10 + 0.5f);
    h.heaterQuarter = (uint8_t)((s.heaterDuty + 2) / 4);
    h.state = (uint8_t)s.state;
    return h;
}

void History::update(const Snapshot &s) {
    if (started && s.uptimeMs - lastMs < kIntervalMs) return;

    // keep a steady 1 s grid; resynchronise after a stall
    lastMs = started && s.uptimeMs - lastMs < 2 * kIntervalMs ? lastMs + kIntervalMs : s.uptimeMs;
    started = true;

    HistorySample h = pack(s);
    samples[next] = h;
    next = (next + 1) % kSamples;
    if (count < kSamples) count++;

    if (curveRecording) {
        curve[curveLen++] = h;
        if (curveLen == kCurve) curveRecording = false;
    }

    // the newest summary keeps changing (peak, ready time) until it is complete
    if (shotsStored && s.hasLastShot && shot(0).id == s.lastShot.id) {
        shots[(shotNext - 1 + kShots) % kShots] = s.lastShot;
    }
}

void History::onEvent(const Event &e) {
    switch (e.type) {
        case EventType::ShotStarted: {
            curveId = e.shot.id;
            curveStart = e.shot.startMs;
            curveLen = 0;
            curveRecording = true;
            // the start is back-dated: copy the seconds since then from the history
            for (int i = 0; i < count && curveLen < kCurve; i++) {
                uint32_t at = lastMs - (uint32_t)(count - 1 - i) * kIntervalMs;
                if ((int32_t)(at - curveStart) >= 0) curve[curveLen++] = sample(i);
            }
            break;
        }
        case EventType::ShotFinished:
            storeShot(e.shot);
            break;
        case EventType::ShotComplete:
            if (shotsStored && shot(0).id == e.shot.id) {
                shots[(shotNext - 1 + kShots) % kShots] = e.shot;
            } else {
                storeShot(e.shot);
            }
            break;
        default:
            break;
    }
}

void History::storeShot(const ShotSummary &shot) {
    shots[shotNext] = shot;
    shotNext = (shotNext + 1) % kShots;
    if (shotsStored < kShots) shotsStored++;
}

}  // namespace silvia
