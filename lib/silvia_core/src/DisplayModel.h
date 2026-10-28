/**
 * @file DisplayModel.h
 * @brief What the SSD1306 shows, decided from the core snapshot alone. The renderer only draws.
 *
 * - Error: full screen, plain language, wins over everything.
 * - Shot: large timer; neutral below 25 s, highlighted 25–30 s, marked too long above 30 s.
 *   The final time is held for 10 s after the shot.
 * - Normal: large temperature, the state in words, the set point.
 * - Network loss is only a small icon, never a full-screen error.
 * - A small coffee cup shows the condition on every screen but the error screen. In standby a
 *   large cup drifts across the dimmed screen; below 40 °C the display goes off.
 * - The normal and shot screens move by up to 2 px every few minutes against OLED burn-in.
 *   Everything that moves is derived from the time alone, never from the number of calls.
 */
#pragma once

#include <stdint.h>

#include "SilviaCore.h"

namespace silvia {

enum class Layout : uint8_t { Booting, Normal, Shot, Error, Standby };

enum class TimerLevel : uint8_t { Neutral, Highlight, TooLong };

/** what the coffee cup shows */
enum class Cup : uint8_t {
    Hidden,   // error screen
    Empty,    // control off
    Heating,  // empty, the steam above it loads with the temperature (cupFill)
    Ready,    // full, animated steam
    Filling,  // shot: fills with the timer (cupFill)
    Standby,  // the large cup drifting across the screen
};

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

    Cup cup;
    float cupFill;           // 0..1: heat-up progress (Heating) or shot progress to 30 s (Filling)
    uint8_t frame;           // steam animation frame 0..kFrames-1
    int8_t shiftX;           // pixel shift of the whole screen, -2..2
    int8_t shiftY;           // -1..1
    int16_t cupX;            // top-left of the drifting standby cup
    int16_t cupY;
    bool displayOn;          // false: the panel is switched off (cold standby)
    bool dim;                // low contrast (standby)
};

class DisplayModel {
  public:
    static constexpr uint32_t kHoldMs = 10000;
    static constexpr float kHighlightS = 25;
    static constexpr float kTooLongS = 30;

    static constexpr int kFrames = 4;
    static constexpr uint32_t kFrameMs = 500;      // the display redraws every 500 ms outside a shot
    static constexpr uint32_t kShiftMs = 180000;   // the screen moves every 3 min
    static constexpr float kDisplayOffBelow = 40;  // °C, cold standby
    static constexpr float kColdTemp = 20;         // °C, start of the heat-up progress
    // drifting standby cup plus the temperature line below it
    static constexpr int kStandbyCupW = 32;
    static constexpr int kStandbyCupH = 32;
    static constexpr int kStandbyTextH = 10;
    static constexpr uint32_t kDriftXMs = 1000;    // ms per pixel
    static constexpr uint32_t kDriftYMs = 1500;

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
