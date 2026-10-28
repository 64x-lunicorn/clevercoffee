/**
 * @file DisplayModel.h
 * @brief What the SSD1306 shows, decided from the core snapshot alone. The renderer only draws.
 *
 * - Error: full screen, plain language, wins over everything.
 * - Shot: large timer; neutral below 25 s, highlighted 25–30 s, marked too long above 30 s.
 *   The final time is held for 10 s after the shot.
 * - Normal: large temperature, the state in words, the set point.
 * - Network loss is only a small icon, never a full-screen error.
 */
#pragma once

#include <stdint.h>

#include "SilviaCore.h"

namespace silvia {

enum class Layout : uint8_t { Booting, Normal, Shot, Error };

enum class TimerLevel : uint8_t { Neutral, Highlight, TooLong };

struct Screen {
    Layout layout;
    float temperature;       // NAN while booting
    float setpoint;
    const char *stateLabel;  // German
    State state;
    ErrorKind error;
    float timerS;
    TimerLevel timerLevel;
    bool timerFinal;         // the held result of a finished shot
    bool wifiLost;
    bool mqttLost;
};

class DisplayModel {
  public:
    static constexpr uint32_t kHoldMs = 10000;
    static constexpr float kHighlightS = 25;
    static constexpr float kTooLongS = 30;

    static TimerLevel levelFor(float seconds);

    /**
     * @param mqttEnabled false until MQTT is configured: then a missing broker is no news
     */
    Screen update(const Snapshot &s, uint32_t nowMs, bool wifiConnected, bool mqttEnabled, bool mqttConnected);

  private:
    bool wasShot = false;
    uint32_t holdUntilMs = 0;
    bool holding = false;
};

}  // namespace silvia
