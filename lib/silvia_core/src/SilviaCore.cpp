#include "SilviaCore.h"

#include <math.h>
#include <string.h>

namespace silvia {

namespace {
const double kEmaFactor = 0.6;  // input smoothing for the D term, as in 3.3.0
}

const char *stateName(State state) {
    switch (state) {
        case State::Off: return "off";
        case State::Booting: return "booting";
        case State::Heating: return "heating";
        case State::Ready: return "ready";
        case State::Shot: return "shot";
        case State::Recovering: return "recovering";
        case State::Steam: return "steam";
        case State::Error: return "error";
        case State::Standby: return "standby";
    }
    return "error";
}

const char *stateLabel(State state) {
    switch (state) {
        case State::Off: return "Aus";
        case State::Booting: return "Start";
        case State::Heating: return "Aufheizen";
        case State::Ready: return "Bereit";
        case State::Shot: return "Bezug";
        case State::Recovering: return "Nachschwingen";
        case State::Steam: return "Dampf";
        case State::Error: return "Fehler";
        case State::Standby: return "Standby";
    }
    return "Fehler";
}

const char *errorName(ErrorKind error) {
    switch (error) {
        case ErrorKind::None: return "none";
        case ErrorKind::Sensor: return "sensor";
        case ErrorKind::OverTemp: return "overtemp";
    }
    return "none";
}

SilviaCore::SilviaCore(ParamStore *store)
    : store(store), pid(&pidInput, &pidOutput, &pidSetpoint, 0, 0, 0, P_ON_E, DIRECT) {}

void SilviaCore::begin(uint32_t nowMs) {
    if (store) values.load(*store);
    values.setRuntime(kSteamOn, 0);
    gate.begin(nowMs);

    pid.SetSampleTime(kWindow);
    pid.SetOutputLimits(0, kWindow);
    pid.SetIntegratorLimits(0, values.get(kPidIMax));
    pid.SetSmoothingFactor(kEmaFactor);
    pid.SetMode(MANUAL);
    pidAutomatic = false;
    pidOutput = 0;
    standby = false;
    lastActivityMs = nowMs;

    fillSnapshot(nowMs, 0);
}

uint16_t SilviaCore::tick(const TickInput &in) {
    bool shotChanged = false;

    if (in.hasReading) {
        float celsius = in.raw - (float)values.get(kTempOffset);
        if (gate.add(in.nowMs, celsius)) {
            float t = gate.value();
            if (!detectorSeeded) {
                detector.reset(t);
                detectorSeeded = true;
            }
            bool armed = values.flag(kPidOn) && mode == Mode::Normal && !gate.error() && !overTemp && !standby;
            shotChanged = detector.add(in.nowMs, t, (float)values.get(kBrewSetpoint),
                                       (float)values.get(kBdSensitivity), armed);
        }
    }
    gate.checkTimeout(in.nowMs);

    updateMode();
    updateErrors(in.nowMs);
    updateShot(in.nowMs, in.unixTime, shotChanged);
    updateStandby(in.nowMs);
    updateState(in.nowMs);
    uint16_t duty = control(in.nowMs);
    fillSnapshot(in.nowMs, duty);
    return duty;
}

void SilviaCore::updateMode() {
    float brewSp = (float)values.get(kBrewSetpoint);
    bool hot = gate.hasValue() && gate.value() >= brewSp + kCoolDownEnd;

    if (values.flag(kSteamOn)) {
        mode = Mode::Steam;
    } else if (mode == Mode::Steam) {
        mode = hot ? Mode::CoolDown : Mode::Normal;
    } else if (mode == Mode::CoolDown && !hot) {
        // 3.3.0 also waited for a rising temperature, which never came with brew PID values of 0
        mode = Mode::Normal;
    }
}

void SilviaCore::updateErrors(uint32_t nowMs) {
    if (gate.hasValue()) {
        float t = gate.value();
        float limit = mode == Mode::Normal ? kOverTemp : kOverTempSteam;
        if (t > limit) {
            overTemp = true;
        } else if (overTemp && t < values.get(kBrewSetpoint) + kOverTempClear) {
            overTemp = false;
        }
    }

    ErrorKind error = gate.error() ? ErrorKind::Sensor : overTemp ? ErrorKind::OverTemp : ErrorKind::None;
    if (error != lastError) {
        Event e = {};
        e.atMs = nowMs;
        if (error == ErrorKind::None) {
            e.type = EventType::ErrorCleared;
            e.error = lastError;
        } else {
            e.type = EventType::ErrorRaised;
            e.error = error;
        }
        push(e);
        lastError = error;
    }
}

void SilviaCore::updateShot(uint32_t nowMs, uint32_t unixTime, bool shotChanged) {
    if (shotChanged) {
        const BrewDetector::Shot &s = detector.shot();
        Event e = {};
        e.atMs = nowMs;
        if (detector.running()) {
            lastActivityMs = nowMs;
            shotCount++;
            e.type = EventType::ShotStarted;
            e.shot.id = shotCount;
            e.shot.startMs = s.startMs;
            e.shot.startUnix = unixTime ? unixTime - (nowMs - s.startMs) / 1000 : 0;
            e.shot.startTemp = s.startTemp;
            e.shot.minTemp = s.minTemp;
            e.shot.peakTemp = NAN;
            e.shot.readyAfterS = NAN;
            if (hasLastShot && !lastShot.complete) {
                lastShot.complete = true;  // a new shot before the last one settled
                Event c = {};
                c.type = EventType::ShotComplete;
                c.atMs = nowMs;
                c.shot = lastShot;
                push(c);
            }
        } else {
            lastShot = {};
            lastShot.id = shotCount;
            lastShot.startMs = s.startMs;
            lastShot.startUnix = unixTime ? unixTime - (nowMs - s.startMs) / 1000 : 0;
            lastShot.durationS = (s.endMs - s.startMs) / 1000.0f;
            lastShot.startTemp = s.startTemp;
            lastShot.minTemp = s.minTemp;
            lastShot.peakTemp = gate.value();
            lastShot.readyAfterS = NAN;
            lastShot.complete = false;
            lastShotEndMs = s.endMs;
            hasLastShot = true;
            e.type = EventType::ShotFinished;
            e.shot = lastShot;
        }
        push(e);
        return;
    }

    if (hasLastShot && !lastShot.complete && !detector.running()) {
        if (gate.hasValue() && !(gate.value() <= lastShot.peakTemp)) lastShot.peakTemp = gate.value();
        if (nowMs - lastShotEndMs >= kSummaryTimeoutMs) {
            lastShot.complete = true;
            Event e = {};
            e.type = EventType::ShotComplete;
            e.atMs = nowMs;
            e.shot = lastShot;
            push(e);
        }
    }
}

void SilviaCore::updateStandby(uint32_t nowMs) {
    // a running shot is activity; an error keeps its own screen but does not stop the clock
    if (standby || detector.running()) return;
    if (nowMs - lastActivityMs >= (uint32_t)values.get(kStandbyMin) * 60000u) standby = true;
}

void SilviaCore::updateState(uint32_t nowMs) {
    State next;
    float brewSp = (float)values.get(kBrewSetpoint);

    if (lastError != ErrorKind::None) {
        next = State::Error;
    } else if (standby) {
        next = State::Standby;
    } else if (!values.flag(kPidOn)) {
        next = State::Off;
    } else if (!gate.hasValue()) {
        next = State::Booting;
    } else if (mode == Mode::Steam) {
        next = State::Steam;
    } else if (detector.running()) {
        next = State::Shot;
    } else if (mode == Mode::CoolDown) {
        next = State::Recovering;
    } else {
        float d = gate.value() - brewSp;
        bool inBand = fabsf(d) <= (ready ? kReadyHold : kReadyBand);
        next = inBand ? State::Ready : d < 0 ? State::Heating : State::Recovering;
    }

    bool nowReady = next == State::Ready;
    if (nowReady && !ready) {
        Event e = {};
        e.type = EventType::ReadyReached;
        e.atMs = nowMs;
        e.firstSinceBoot = !everReady;
        push(e);
        everReady = true;
        if (hasLastShot && !lastShot.complete && lastShot.readyAfterS != lastShot.readyAfterS) {
            lastShot.readyAfterS = (nowMs - lastShotEndMs) / 1000.0f;
        }
    }
    ready = nowReady;
    state = next;
}

void SilviaCore::applyTunings(double kp, double tn, double tv) {
    pid.SetTunings(kp, tn != 0 ? kp / tn : 0, tv * kp, P_ON_E);
}

uint16_t SilviaCore::control(uint32_t nowMs) {
    bool heat = values.flag(kPidOn) && gate.hasValue() && lastError == ErrorKind::None && !standby;
    brewPidActive = false;
    if (!heat) {
        if (pidAutomatic) {
            pid.SetMode(MANUAL);
            pidAutomatic = false;
        }
        pidOutput = 0;
        return 0;
    }

    pid.SetIntegratorLimits(0, values.get(kPidIMax));
    // brew PID: tunings switch at the detection and stay for the brew PID time (no delay any more)
    bool brewPid = values.flag(kBdPidOn) && shotCount > 0 && mode == Mode::Normal &&
                   nowMs - detector.detectedAtMs() < values.get(kBdTime) * 1000;
    brewPidActive = brewPid;

    if (mode == Mode::Steam) {
        pidSetpoint = values.get(kSteamSetpoint);
        pid.SetTunings(values.get(kSteamKp), 0, 0, P_ON_E);
    } else if (mode == Mode::CoolDown || brewPid) {
        pidSetpoint = values.get(kBrewSetpoint);
        applyTunings(values.get(kBdKp), values.get(kBdTn), values.get(kBdTv));
    } else {
        pidSetpoint = values.get(kBrewSetpoint);
        applyTunings(values.get(kPidKp), values.get(kPidTn), values.get(kPidTv));
    }

    pidInput = gate.value();
    if (!pidAutomatic) {
        pid.SetMode(AUTOMATIC);
        pidAutomatic = true;
    }
    pid.Compute();

    double out = pidOutput < 0 ? 0 : pidOutput > kWindow ? kWindow : pidOutput;
    return (uint16_t)(out + 0.5);
}

void SilviaCore::fillSnapshot(uint32_t nowMs, uint16_t duty) {
    snap.uptimeMs = nowMs;
    snap.temperature = gate.hasValue() ? gate.value() : NAN;
    snap.brewSetpoint = (float)values.get(kBrewSetpoint);
    snap.steamSetpoint = (float)values.get(kSteamSetpoint);
    snap.setpoint = mode == Mode::Steam ? snap.steamSetpoint : snap.brewSetpoint;
    snap.heaterDuty = duty;
    snap.state = state;
    snap.error = lastError;
    snap.ready = ready;
    snap.pidOn = values.flag(kPidOn);
    snap.steamOn = values.flag(kSteamOn);
    snap.brewPidActive = brewPidActive;
    snap.shotTimerS = detector.running() ? (nowMs - detector.shot().startMs) / 1000.0f : 0;
    snap.shotCount = shotCount;
    snap.hasLastShot = hasLastShot;
    snap.lastShot = lastShot;
}

size_t SilviaCore::setParams(const char *const *keys, const char *const *texts, size_t count, FieldError *errors,
                             size_t errorCap) {
    ParamSet::Write writes[kParamCount];
    size_t writeCount = 0;
    size_t rejected = 0;

    for (size_t i = 0; i < count; i++) {
        ParamError error;
        double value = 0;
        ParamId id = findParam(keys[i]);
        if (isReadOnlyKey(keys[i])) {
            error = ParamError::ReadOnly;
        } else if (id == kParamCount) {
            error = ParamError::Unknown;
        } else {
            error = parseParamValue(id, texts[i], value);
        }

        if (error != ParamError::None) {
            if (rejected < errorCap) {
                strncpy(errors[rejected].key, keys[i], sizeof(errors[rejected].key) - 1);
                errors[rejected].key[sizeof(errors[rejected].key) - 1] = '\0';
                errors[rejected].error = error;
            }
            rejected++;
            continue;
        }

        size_t w = 0;
        while (w < writeCount && writes[w].id != id) w++;  // a repeated key: the last one wins
        writes[w] = {id, value};
        if (w == writeCount) writeCount++;
    }

    if (rejected == 0) {
        values.apply(writes, writeCount, store);
        if (writeCount > 0) lastActivityMs = snap.uptimeMs;  // stored in standby too, but no wake-up
    }
    return rejected;
}

ParamError SilviaCore::setParam(const char *key, const char *value) {
    FieldError error = {};
    return setParams(&key, &value, 1, &error, 1) ? error.error : ParamError::None;
}

void SilviaCore::push(const Event &event) {
    if (eventCount == kEventCap) {  // drop the oldest
        eventHead = (eventHead + 1) % kEventCap;
        eventCount--;
    }
    events[(eventHead + eventCount) % kEventCap] = event;
    eventCount++;
}

bool SilviaCore::popEvent(Event &event) {
    if (eventCount == 0) return false;
    event = events[eventHead];
    eventHead = (eventHead + 1) % kEventCap;
    eventCount--;
    return true;
}

}  // namespace silvia
