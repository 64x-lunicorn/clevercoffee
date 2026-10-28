#include "Json.h"

#include <math.h>
#include <stdio.h>

#include "CorePlatform.h"

namespace silvia {

JsonOut::JsonOut(char *buffer, size_t capacity) : buf(buffer), cap(capacity) {
    if (cap) buf[0] = '\0';
}

void JsonOut::put(char c) {
    if (len + 1 >= cap) {
        over = true;
        return;
    }
    buf[len++] = c;
    buf[len] = '\0';
}

void JsonOut::puts(const char *s) {
    while (*s) put(*s++);
}

void JsonOut::separator() {
    if (needComma) put(',');
    needComma = true;
}

void JsonOut::escaped(char c) {
    switch (c) {
        case '"': puts("\\\""); break;
        case '\\': puts("\\\\"); break;
        case '\n': puts("\\n"); break;
        case '\r': puts("\\r"); break;
        case '\t': puts("\\t"); break;
        default:
            if ((unsigned char)c < 0x20) {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)c);
                puts(hex);
            } else {
                put(c);
            }
    }
}

JsonOut &JsonOut::beginObject() {
    separator();
    put('{');
    needComma = false;
    return *this;
}

JsonOut &JsonOut::endObject() {
    put('}');
    needComma = true;
    return *this;
}

JsonOut &JsonOut::beginArray() {
    separator();
    put('[');
    needComma = false;
    return *this;
}

JsonOut &JsonOut::endArray() {
    put(']');
    needComma = true;
    return *this;
}

JsonOut &JsonOut::key(const char *name) {
    separator();
    put('"');
    while (*name) escaped(*name++);
    puts("\":");
    needComma = false;
    return *this;
}

JsonOut &JsonOut::str(const char *value) {
    separator();
    put('"');
    while (*value) escaped(*value++);
    put('"');
    return *this;
}

JsonOut &JsonOut::strP(const char *flashValue) {
    separator();
    put('"');
    for (char c; (c = CORE_READ_BYTE(flashValue)) != '\0'; flashValue++) escaped(c);
    put('"');
    return *this;
}

JsonOut &JsonOut::num(double value, int decimals) {
    if (value - value != 0) return null();  // NAN or infinite (no isinf on the old toolchain)
    separator();
    char text[32];
    snprintf(text, sizeof(text), "%.*f", decimals, value);
    // "-0.0" reads oddly in a UI
    if (text[0] == '-') {
        bool zero = true;
        for (const char *p = text + 1; *p; p++) {
            if (*p != '0' && *p != '.') zero = false;
        }
        if (zero) {
            puts(text + 1);
            return *this;
        }
    }
    puts(text);
    return *this;
}

JsonOut &JsonOut::integer(long value) {
    separator();
    char text[16];
    snprintf(text, sizeof(text), "%ld", value);
    puts(text);
    return *this;
}

JsonOut &JsonOut::boolean(bool value) {
    separator();
    puts(value ? "true" : "false");
    return *this;
}

JsonOut &JsonOut::null() {
    separator();
    puts("null");
    return *this;
}

JsonOut &JsonOut::raw(const char *text) {
    separator();
    puts(text);
    return *this;
}

void writeShot(JsonOut &out, const ShotSummary &shot) {
    out.beginObject();
    out.key("id").integer(shot.id);
    out.key("start_uptime_s").num(shot.startMs / 1000.0, 1);
    out.key("start_unix");
    if (shot.startUnix) out.integer(shot.startUnix); else out.null();
    out.key("duration_s").num(shot.durationS, 1);
    out.key("start_temp").num(shot.startTemp, 1);
    out.key("min_temp").num(shot.minTemp, 1);
    out.key("peak_temp").num(shot.peakTemp, 1);
    out.key("ready_after_s").num(shot.readyAfterS, 0);
    out.key("complete").boolean(shot.complete);
    out.endObject();
}

void writeState(JsonOut &out, const Snapshot &s, const DeviceInfo &device) {
    out.beginObject();
    out.key("temperature").num(s.temperature, 1);
    out.key("setpoint").num(s.setpoint, 1);
    out.key("brew_setpoint").num(s.brewSetpoint, 1);
    out.key("steam_setpoint").num(s.steamSetpoint, 1);
    out.key("heater").num(s.heaterDuty / 10.0, 1);
    out.key("state").str(stateName(s.state));
    out.key("state_label").str(stateLabel(s.state));
    out.key("ready").boolean(s.ready);
    out.key("error").str(errorName(s.error));
    out.key("pid_on").boolean(s.pidOn);
    out.key("steam_on").boolean(s.steamOn);
    out.key("brew_pid_active").boolean(s.brewPidActive);
    out.key("shot_timer_s");
    if (s.state == State::Shot) out.num(s.shotTimerS, 1); else out.null();
    out.key("shot_count").integer(s.shotCount);
    out.key("last_shot");
    if (s.hasLastShot) writeShot(out, s.lastShot); else out.null();
    out.key("uptime_s").integer(s.uptimeMs / 1000);
    out.key("wifi_rssi");
    if (device.wifiConnected) out.integer(device.wifiRssi); else out.null();
    out.key("mqtt_connected").boolean(device.mqttConnected);
    out.key("free_heap").integer(device.freeHeap);
    out.key("version").str(device.version);
    out.endObject();
}

void writeParameters(JsonOut &out, const ParamSet &params) {
    out.beginArray();
    for (int i = 0; i < kParamCount; i++) {
        const ParamDef &d = paramDef((ParamId)i);
        out.beginObject();
        out.key("key").str(d.key);
        out.key("label").strP(d.label);
        out.key("help").strP(d.help);
        out.key("type").str(d.type == ParamType::Bool ? "bool" : "number");
        out.key("unit").str(d.unit);
        out.key("value");
        if (d.type == ParamType::Bool) out.boolean(params.flag((ParamId)i)); else out.num(params.get((ParamId)i), 2);
        out.key("min").num(d.min, 1);
        out.key("max").num(d.max, 1);
        out.key("step").num(d.step, 1);
        out.key("default").num(d.def, 1);
        out.key("expert").boolean(d.flags & kExpert);
        out.key("persisted").boolean(d.slot != kSlotNone);
        out.endObject();
    }
    out.endArray();
}

void writeFieldErrors(JsonOut &out, const FieldError *errors, size_t count) {
    out.beginObject();
    out.key("errors").beginObject();
    for (size_t i = 0; i < count; i++) {
        out.key(errors[i].key).beginObject();
        out.key("code").str(paramErrorCode(errors[i].error));
        ParamId id = findParam(errors[i].key);
        if (errors[i].error == ParamError::OutOfRange && id != kParamCount) {
            out.key("min").num(paramDef(id).min, 1);
            out.key("max").num(paramDef(id).max, 1);
        }
        out.endObject();
    }
    out.endObject();
    out.endObject();
}

}  // namespace silvia
