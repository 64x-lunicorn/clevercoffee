/**
 * @file Mqtt.h
 * @brief Home Assistant over MQTT: discovery documents, state/settings/event payloads, the
 *        publish cadence and command handling. The adapter (src/MqttLink) only moves bytes.
 *
 * Topics (prefix "silvia"):
 *   silvia/status          online/offline, retained, offline is the last will
 *   silvia/state           measurements and state (JSON)
 *   silvia/settings        set points and switches, PID parameters (JSON, retained)
 *   silvia/event/shot      one message per finished shot (JSON)
 *   silvia/set/<key>       commands: brew_setpoint, steam_setpoint, steam_on, pid_on
 *   homeassistant/status   HA birth message: discovery is published again on "online"
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Json.h"
#include "SilviaCore.h"

namespace silvia {
namespace mqtt {

extern const char *const kAvailabilityTopic;  // "silvia/status"
extern const char *const kStateTopic;
extern const char *const kSettingsTopic;
extern const char *const kShotEventTopic;
extern const char *const kCommandFilter;      // "silvia/set/+"
extern const char *const kHaStatusTopic;      // "homeassistant/status"

/** number of discovery documents (one per Home Assistant entity) */
int discoveryCount();

/**
 * @brief discovery document i
 * @param configUrl e.g. "http://192.168.1.15/", shown as the device's web link in HA
 * @return false if i is out of range
 */
bool discovery(int i, const char *version, const char *configUrl, char *topic, size_t topicCap, JsonOut &out);

void writeState(JsonOut &out, const Snapshot &s, const DeviceInfo &device);
void writeSettings(JsonOut &out, const ParamSet &params);
void writeShotEvent(JsonOut &out, const ShotSummary &shot);

/** state every second while something happens, every 10 s when idle, at once on a change */
class Cadence {
  public:
    static constexpr uint32_t kActiveMs = 1000;
    static constexpr uint32_t kIdleMs = 10000;

    static bool idle(const Snapshot &s);
    bool due(uint32_t nowMs, const Snapshot &s);
    void reset() { started = false; }

  private:
    bool started = false;
    uint32_t lastMs = 0;
    State lastState = State::Booting;
    bool lastReady = false;
    ErrorKind lastError = ErrorKind::None;
};

/**
 * @brief apply a command topic through the parameter table
 * @return ParamError::Unknown for topics outside silvia/set/ or parameters HA may not set
 */
ParamError handleCommand(SilviaCore &core, const char *topic, const char *payload);

}  // namespace mqtt
}  // namespace silvia
