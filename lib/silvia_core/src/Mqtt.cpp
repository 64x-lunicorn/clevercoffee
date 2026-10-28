#include "Mqtt.h"

#include <stdio.h>
#include <string.h>

namespace silvia {
namespace mqtt {

const char *const kAvailabilityTopic = "silvia/status";
const char *const kStateTopic = "silvia/state";
const char *const kSettingsTopic = "silvia/settings";
const char *const kShotEventTopic = "silvia/event/shot";
const char *const kCommandFilter = "silvia/set/+";
const char *const kHaStatusTopic = "homeassistant/status";

namespace {

const char *const kCommandPrefix = "silvia/set/";

enum class Source : uint8_t { State, Settings, Event };

struct Entity {
    const char *component;
    const char *id;           // entity ids become e.g. sensor.silvia_temperature
    const char *name;         // German, shown in HA (the device name "Silvia" is prefixed)
    Source source;
    const char *field;        // JSON field in the source payload
    const char *deviceClass;  // or nullptr
    const char *unit;         // or nullptr
    const char *icon;         // or nullptr
    bool diagnostic;          // entity_category diagnostic, disabled by default
};

const Entity entities[] = {
    {"sensor", "temperature", "Temperatur", Source::State, "temperature", "temperature", "°C", nullptr, false},
    {"sensor", "heater", "Heizleistung", Source::State, "heater", nullptr, "%", "mdi:radiator", false},
    {"sensor", "state", "Zustand", Source::State, "state", "enum", nullptr, "mdi:coffee-maker", false},
    {"binary_sensor", "ready", "Bereit", Source::State, "ready", nullptr, nullptr, "mdi:coffee", false},
    {"sensor", "error", "Fehler", Source::State, "error", "enum", nullptr, "mdi:alert", false},
    {"sensor", "last_shot", "Letzter Shot", Source::State, "last_shot_s", "duration", "s", "mdi:timer-outline",
     false},
    {"sensor", "shot_count", "Shots seit Einschalten", Source::State, "shot_count", nullptr, nullptr,
     "mdi:counter", false},
    {"number", "brew_setpoint", "Brühtemperatur", Source::Settings, "brew_setpoint", "temperature", "°C",
     nullptr, false},
    {"number", "steam_setpoint", "Dampftemperatur", Source::Settings, "steam_setpoint", "temperature", "°C",
     nullptr, false},
    {"switch", "steam", "Dampf", Source::Settings, "steam_on", nullptr, nullptr, "mdi:kettle-steam", false},
    {"switch", "pid", "Regelung", Source::Settings, "pid_on", nullptr, nullptr, "mdi:thermostat", false},
    {"event", "shot", "Shot", Source::Event, nullptr, nullptr, nullptr, "mdi:coffee-outline", false},
    {"sensor", "wifi_signal", "WLAN-Signal", Source::State, "rssi", "signal_strength", "dBm", nullptr, true},
    {"sensor", "firmware", "Firmware", Source::State, "version", nullptr, nullptr, "mdi:chip", true},
    {"sensor", "pid_kp", "PID Kp", Source::Settings, "pid_kp", nullptr, nullptr, "mdi:tune", true},
    {"sensor", "pid_tn", "PID Tn", Source::Settings, "pid_tn", "duration", "s", "mdi:tune", true},
    {"sensor", "pid_tv", "PID Tv", Source::Settings, "pid_tv", "duration", "s", "mdi:tune", true},
    {"sensor", "pid_i_max", "PID I-Max", Source::Settings, "pid_i_max", nullptr, nullptr, "mdi:tune", true},
};
const int kEntityCount = sizeof(entities) / sizeof(entities[0]);

const char *topicFor(Source source) {
    switch (source) {
        case Source::State: return kStateTopic;
        case Source::Settings: return kSettingsTopic;
        case Source::Event: return kShotEventTopic;
    }
    return kStateTopic;
}

void valueTemplate(JsonOut &out, const Entity &e, bool onOff) {
    char tpl[96];
    if (onOff) {
        snprintf(tpl, sizeof tpl, "{{ 'ON' if value_json.%s else 'OFF' }}", e.field);
    } else {
        snprintf(tpl, sizeof tpl, "{{ value_json.%s }}", e.field);
    }
    out.key("value_template").str(tpl);
}

void enumOptions(JsonOut &out, const Entity &e) {
    out.key("options").beginArray();
    if (strcmp(e.id, "state") == 0) {
        for (int s = (int)State::Off; s <= (int)State::Error; s++) out.str(stateName((State)s));
    } else {
        for (int k = (int)ErrorKind::None; k <= (int)ErrorKind::OverTemp; k++) out.str(errorName((ErrorKind)k));
    }
    out.endArray();
}

}  // namespace

int discoveryCount() { return kEntityCount; }

bool discovery(int i, const char *version, const char *configUrl, char *topic, size_t topicCap, JsonOut &out) {
    if (i < 0 || i >= kEntityCount) return false;
    const Entity &e = entities[i];
    snprintf(topic, topicCap, "homeassistant/%s/silvia/%s/config", e.component, e.id);

    char id[40];
    snprintf(id, sizeof id, "silvia_%s", e.id);
    char entityId[56];  // e.g. sensor.silvia_temperature (object_id is deprecated since HA 2025.10)
    snprintf(entityId, sizeof entityId, "%s.%s", e.component, id);

    out.beginObject();
    out.key("name").str(e.name);
    out.key("unique_id").str(id);
    out.key("default_entity_id").str(entityId);
    out.key("availability_topic").str(kAvailabilityTopic);
    out.key("state_topic").str(topicFor(e.source));
    if (e.deviceClass) out.key("device_class").str(e.deviceClass);
    if (e.unit) out.key("unit_of_measurement").str(e.unit);
    if (e.icon) out.key("icon").str(e.icon);
    if (e.diagnostic) {
        out.key("entity_category").str("diagnostic");
        out.key("enabled_by_default").boolean(false);
    }

    bool isSwitch = strcmp(e.component, "switch") == 0;
    bool isBinary = strcmp(e.component, "binary_sensor") == 0;
    if (strcmp(e.component, "event") == 0) {
        out.key("event_types").beginArray().str("shot").endArray();
    } else {
        valueTemplate(out, e, isSwitch || isBinary);
    }

    if (e.deviceClass && strcmp(e.deviceClass, "enum") == 0) enumOptions(out, e);
    if (strcmp(e.component, "sensor") == 0 && e.unit && !e.diagnostic) {
        out.key("state_class").str("measurement");
    }
    if (strcmp(e.id, "temperature") == 0) out.key("suggested_display_precision").integer(1);
    if (strcmp(e.id, "shot_count") == 0) out.key("state_class").str("total_increasing");

    if (strcmp(e.component, "number") == 0 || isSwitch) {
        char cmd[48];
        snprintf(cmd, sizeof cmd, "%s%s", kCommandPrefix, e.field);
        out.key("command_topic").str(cmd);
    }
    if (strcmp(e.component, "number") == 0) {
        const ParamDef &d = paramDef(findParam(e.field));
        out.key("min").num(d.min, 1);
        out.key("max").num(d.max, 1);
        out.key("step").num(d.step, 1);
        out.key("mode").str("box");
    }
    if (isSwitch || isBinary) {
        out.key("payload_on").str("ON");
        out.key("payload_off").str("OFF");
    }

    out.key("device").beginObject();
    out.key("identifiers").beginArray().str("silvia").endArray();
    out.key("name").str("Silvia");
    out.key("manufacturer").str("Rancilio");
    out.key("model").str("Silvia E");
    out.key("sw_version").str(version);
    if (configUrl && *configUrl) out.key("configuration_url").str(configUrl);
    out.endObject();

    out.endObject();
    return true;
}

void writeState(JsonOut &out, const Snapshot &s, const DeviceInfo &device) {
    out.beginObject();
    out.key("temperature").num(s.temperature, 1);
    out.key("setpoint").num(s.setpoint, 1);
    out.key("heater").num(s.heaterDuty / 10.0, 1);
    out.key("state").str(stateName(s.state));
    out.key("ready").boolean(s.ready);
    out.key("error").str(errorName(s.error));
    out.key("shot_timer_s");
    if (s.state == State::Shot) out.num(s.shotTimerS, 0); else out.null();
    out.key("last_shot_s");
    if (s.hasLastShot) out.num(s.lastShot.durationS, 1); else out.null();
    out.key("shot_count").integer(s.shotCount);
    out.key("rssi");
    if (device.wifiConnected) out.integer(device.wifiRssi); else out.null();
    out.key("uptime_s").integer(s.uptimeMs / 1000);
    out.key("version").str(device.version);
    out.endObject();
}

void writeSettings(JsonOut &out, const ParamSet &params) {
    out.beginObject();
    out.key("brew_setpoint").num(params.get(kBrewSetpoint), 1);
    out.key("steam_setpoint").num(params.get(kSteamSetpoint), 1);
    out.key("steam_on").boolean(params.flag(kSteamOn));
    out.key("pid_on").boolean(params.flag(kPidOn));
    out.key("pid_kp").num(params.get(kPidKp), 1);
    out.key("pid_tn").num(params.get(kPidTn), 1);
    out.key("pid_tv").num(params.get(kPidTv), 1);
    out.key("pid_i_max").num(params.get(kPidIMax), 0);
    out.key("bd_pid_on").boolean(params.flag(kBdPidOn));
    out.endObject();
}

void writeShotEvent(JsonOut &out, const ShotSummary &shot) {
    out.beginObject();
    out.key("event_type").str("shot");
    out.key("id").integer(shot.id);
    out.key("duration_s").num(shot.durationS, 1);
    out.key("start_temp").num(shot.startTemp, 1);
    out.key("min_temp").num(shot.minTemp, 1);
    out.key("start_unix");
    if (shot.startUnix) out.integer(shot.startUnix); else out.null();
    out.endObject();
}

bool Cadence::idle(const Snapshot &s) { return s.state == State::Ready || s.state == State::Off; }

bool Cadence::due(uint32_t nowMs, const Snapshot &s) {
    bool changed = !started || s.state != lastState || s.ready != lastReady || s.error != lastError;
    bool elapsed = nowMs - lastMs >= (idle(s) ? kIdleMs : kActiveMs);
    if (!changed && !elapsed) return false;
    started = true;
    lastMs = nowMs;
    lastState = s.state;
    lastReady = s.ready;
    lastError = s.error;
    return true;
}

ParamError handleCommand(SilviaCore &core, const char *topic, const char *payload) {
    size_t prefix = strlen(kCommandPrefix);
    if (strncmp(topic, kCommandPrefix, prefix) != 0) return ParamError::Unknown;
    const char *key = topic + prefix;
    if (isReadOnlyKey(key)) return ParamError::ReadOnly;
    ParamId id = findParam(key);
    // HA controls only what the discovery offers; expert values need the web password
    if (id == kParamCount || !(paramDef(id).flags & kMqtt) || strcmp(paramDef(id).key, key) != 0) {
        return ParamError::Unknown;
    }
    return core.setParam(key, payload);
}

}  // namespace mqtt
}  // namespace silvia
