#include "DisplayModel.h"

namespace silvia {

namespace {

// positions of the whole screen, one per kShiftMs; each step moves by at most 1 px
const int8_t kShift[][2] = {{0, 0}, {1, 0}, {2, 0}, {1, 1}, {0, 1}, {-1, 1}, {-2, 0}, {-1, -1}, {0, -1}, {1, -1}};
const int kShiftCount = sizeof(kShift) / sizeof(kShift[0]);

// 0, 1, .., span, span-1, .., 1, 0, 1, ..
int16_t triangle(uint32_t steps, int span) {
    if (span <= 0) return 0;
    uint32_t p = steps % (2u * span);
    return (int16_t)(p <= (uint32_t)span ? p : 2u * span - p);
}

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

}  // namespace

TimerLevel DisplayModel::levelFor(float seconds) {
    if (seconds > kTooLongS) return TimerLevel::TooLong;
    if (seconds >= kHighlightS) return TimerLevel::Highlight;
    return TimerLevel::Neutral;
}

Screen DisplayModel::update(const Snapshot &s, uint32_t nowMs, bool wifiConnected, bool mqttEnabled,
                            bool mqttConnected) {
    Screen screen = {};
    screen.temperature = s.temperature;
    screen.setpoint = s.setpoint;
    screen.state = s.state;
    screen.stateLabel = stateLabel(s.state);
    screen.error = s.error;
    screen.wifiLost = !wifiConnected;
    screen.mqttLost = mqttEnabled && wifiConnected && !mqttConnected;

    bool isShot = s.state == State::Shot;
    if (wasShot && !isShot && s.hasLastShot) {
        holding = true;
        holdUntilMs = nowMs + kHoldMs;
    }
    if (isShot || (holding && (int32_t)(nowMs - holdUntilMs) >= 0)) holding = false;
    wasShot = isShot;

    if (s.state == State::Error) {
        screen.layout = Layout::Error;
    } else if (isShot) {
        screen.layout = Layout::Shot;
        screen.timerS = s.shotTimerS;
    } else if (holding) {
        screen.layout = Layout::Shot;
        screen.timerS = s.lastShot.durationS;
        screen.timerFinal = true;
    } else if (nowMs < kSplashMs) {
        screen.layout = Layout::Splash;
    } else if (s.state == State::Standby) {
        screen.layout = Layout::Standby;
    } else if (s.state == State::Booting) {
        screen.layout = Layout::Booting;
    } else {
        screen.layout = Layout::Normal;
    }
    screen.timerLevel = levelFor(screen.timerS);

    screen.frame = (uint8_t)((nowMs / kFrameMs) % kFrames);
    screen.displayOn = true;
    switch (screen.layout) {
        case Layout::Error:
        case Layout::Splash: screen.cup = Cup::Hidden; break;
        case Layout::Standby:
            screen.cup = Cup::Standby;
            screen.dim = true;
            screen.displayOn = !(s.temperature < kDisplayOffBelow);  // NAN: keep it on
            screen.cupX = triangle(nowMs / kDriftXMs, 128 - kStandbyCupW);
            screen.cupY = triangle(nowMs / kDriftYMs, 64 - kStandbyCupH - kStandbyTextH);
            break;
        case Layout::Shot:
            screen.cup = Cup::Filling;
            screen.cupFill = clamp01(screen.timerS / kTooLongS);
            break;
        case Layout::Booting:
        case Layout::Normal:
            if (s.state == State::Off) {
                screen.cup = Cup::Empty;
            } else if (s.state == State::Booting || s.state == State::Heating) {
                screen.cup = Cup::Heating;
                float span = s.setpoint - kColdTemp;
                screen.cupFill = span > 0 ? clamp01((s.temperature - kColdTemp) / span) : 0;
                if (screen.cupFill != screen.cupFill) screen.cupFill = 0;  // no reading yet
            } else {
                screen.cup = Cup::Ready;  // ready, recovering, steam
            }
            break;
    }
    if (screen.layout == Layout::Normal || screen.layout == Layout::Shot) {
        const int8_t *p = kShift[(nowMs / kShiftMs) % kShiftCount];
        screen.shiftX = p[0];
        screen.shiftY = p[1];
    }
    return screen;
}

}  // namespace silvia
