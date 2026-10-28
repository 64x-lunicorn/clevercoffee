#include "DisplayModel.h"

namespace silvia {

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
    } else if (s.state == State::Booting) {
        screen.layout = Layout::Booting;
    } else {
        screen.layout = Layout::Normal;
    }
    screen.timerLevel = levelFor(screen.timerS);
    return screen;
}

}  // namespace silvia
