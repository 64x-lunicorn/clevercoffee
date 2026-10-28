/**
 * @file SilviaCore.h
 * @brief All control decisions of the Silvia, without hardware.
 *
 * One seam: per tick the core gets the time and, if there is one, the latest raw sensor
 * reading. It returns the heater duty (0..1000 per 1 s window), a state snapshot and events.
 * Parameter writes and commands are validated against the parameter table before they reach
 * the controller.
 *
 * The PID library reads millis() itself; on the device that is the same clock as nowMs, on
 * the host the tests set the stubbed millis() to nowMs.
 */
#pragma once

#include <PID_v1.h>
#include <stddef.h>
#include <stdint.h>

#include "BrewDetector.h"
#include "Params.h"
#include "SensorGate.h"

namespace silvia {

/** machine state as shown to the user (web, display, Home Assistant) */
enum class State : uint8_t { Off, Booting, Heating, Ready, Shot, Recovering, Steam, Error };

enum class ErrorKind : uint8_t { None, Sensor, OverTemp };

const char *stateName(State state);      // "off", "booting", ...
const char *stateLabel(State state);     // German, for the UI and display
const char *errorName(ErrorKind error);  // "none", "sensor", "overtemp"

struct ShotSummary {
    uint32_t id;           // counts up from 1 since boot
    uint32_t startMs;      // uptime at the (back-dated) start
    uint32_t startUnix;    // 0 if the clock was not set
    float durationS;
    float startTemp;
    float minTemp;
    float peakTemp;        // highest temperature after the shot until ready again (NAN until known)
    float readyAfterS;     // seconds from the end until ready again (NAN until known)
    bool complete;         // peak and ready time are final
};

struct Snapshot {
    uint32_t uptimeMs;
    float temperature;     // NAN until the first valid reading
    float setpoint;        // the active one (brew or steam)
    float brewSetpoint;
    float steamSetpoint;
    uint16_t heaterDuty;   // 0..1000 per 1 s window
    State state;
    ErrorKind error;
    bool ready;
    bool pidOn;
    bool steamOn;
    bool brewPidActive;
    float shotTimerS;      // running shot: seconds since the back-dated start, else 0
    uint32_t shotCount;    // shots since boot
    bool hasLastShot;
    ShotSummary lastShot;
};

enum class EventType : uint8_t {
    ShotStarted,
    ShotFinished,    // duration, start and minimum known
    ShotComplete,    // peak and time until ready known as well
    ErrorRaised,
    ErrorCleared,
    ReadyReached,    // firstSinceBoot: the heat-up after power-on is done
};

struct Event {
    EventType type;
    uint32_t atMs;
    ErrorKind error;
    bool firstSinceBoot;
    ShotSummary shot;
};

struct TickInput {
    uint32_t nowMs;
    bool hasReading;
    float raw;           // °C as read from the sensor (before the offset)
    uint32_t unixTime;   // 0 if unknown
};

struct FieldError {
    char key[24];
    ParamError error;
};

class SilviaCore {
  public:
    static constexpr float kOverTemp = 120;        // °C
    static constexpr float kOverTempSteam = 145;   // °C, in steam mode and the cool-down after it
    static constexpr float kOverTempClear = 5;     // °C above the brew set point
    static constexpr float kReadyBand = 0.5f;      // °C around the set point to become ready
    static constexpr float kReadyHold = 1.0f;      // °C around the set point to stay ready
    static constexpr float kCoolDownEnd = 2;       // °C above the brew set point
    static constexpr uint32_t kSummaryTimeoutMs = 300000;
    static constexpr uint16_t kWindow = 1000;      // heater PWM window and PID output range

    explicit SilviaCore(ParamStore *store = nullptr);

    void begin(uint32_t nowMs);

    /** @return the heater duty to apply right away (0..1000) */
    uint16_t tick(const TickInput &in);

    const Snapshot &snapshot() const { return snap; }
    const ParamSet &params() const { return values; }

    /**
     * @brief validate and apply a batch of text values (form fields, API body, MQTT payloads)
     * @param errors receives one entry per rejected field (may be null if errorCap is 0)
     * @return number of rejected fields; nothing is applied if it is not 0
     */
    size_t setParams(const char *const *keys, const char *const *values, size_t count, FieldError *errors,
                     size_t errorCap);

    /** single parameter convenience (MQTT command topics) */
    ParamError setParam(const char *key, const char *value);

    /** events since the last call, oldest first */
    bool popEvent(Event &event);

  private:
    enum class Mode : uint8_t { Normal, Steam, CoolDown };

    void updateTemperature(const TickInput &in);
    void updateMode();
    void updateErrors(uint32_t nowMs);
    void updateShot(uint32_t nowMs, uint32_t unixTime, bool shotChanged);
    void updateState(uint32_t nowMs);
    uint16_t control(uint32_t nowMs);
    void applyTunings(double kp, double tn, double tv);
    void push(const Event &event);
    void fillSnapshot(uint32_t nowMs, uint16_t duty);

    ParamStore *store;
    ParamSet values;
    SensorGate gate;
    BrewDetector detector;

    double pidInput = 0;
    double pidOutput = 0;
    double pidSetpoint = 0;
    PID pid;
    bool pidAutomatic = false;
    bool brewPidActive = false;

    Mode mode = Mode::Normal;
    bool detectorSeeded = false;
    bool overTemp = false;
    ErrorKind lastError = ErrorKind::None;
    State state = State::Booting;
    bool ready = false;
    bool everReady = false;

    uint32_t shotCount = 0;
    bool hasLastShot = false;
    ShotSummary lastShot = {};
    uint32_t lastShotEndMs = 0;

    Snapshot snap = {};

    static constexpr int kEventCap = 8;
    Event events[kEventCap];
    int eventHead = 0;
    int eventCount = 0;
};

}  // namespace silvia
