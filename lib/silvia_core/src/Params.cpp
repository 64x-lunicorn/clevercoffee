#include "Params.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "CorePlatform.h"

namespace silvia {

namespace {

// labels and help texts live in flash on the ESP8266 (read them with CORE_READ_BYTE)
const char lPidOn[] CORE_PROGMEM = "Regelung an";
const char hPidOn[] CORE_PROGMEM = "Aus = Heizung bleibt aus, die Maschine wird nicht geregelt.";
const char lBrewSp[] CORE_PROGMEM = "Brühtemperatur";
const char hBrewSp[] CORE_PROGMEM = "Solltemperatur am Kessel-Fühler für Espresso.";
const char lSteamSp[] CORE_PROGMEM = "Dampftemperatur";
const char hSteamSp[] CORE_PROGMEM = "Solltemperatur im Dampfmodus.";
const char lSteamOn[] CORE_PROGMEM = "Dampfmodus";
const char hSteamOn[] CORE_PROGMEM = "Heizt auf die Dampftemperatur. Wird beim Neustart nicht gespeichert.";
const char lOffset[] CORE_PROGMEM = "Temperatur-Offset";
const char hOffset[] CORE_PROGMEM =
    "Wird von jedem Messwert abgezogen, z. B. um die Differenz zwischen Kessel und Brühgruppe "
    "auszugleichen. 0 = Fühlerwert wird direkt verwendet.";
const char lKp[] CORE_PROGMEM = "PID Kp";
const char hKp[] CORE_PROGMEM =
    "Proportionalanteil (Heizleistung pro °C Abweichung). Größer = kräftiger, aber unruhiger.";
const char lTn[] CORE_PROGMEM = "PID Tn";
const char hTn[] CORE_PROGMEM =
    "Nachstellzeit in s (Ki = Kp/Tn). Größer = Integralanteil wächst langsamer. 0 = kein I-Anteil.";
const char lTv[] CORE_PROGMEM = "PID Tv";
const char hTv[] CORE_PROGMEM = "Vorhaltezeit in s (Kd = Kp·Tv). Dämpft schnelle Temperaturänderungen.";
const char lIMax[] CORE_PROGMEM = "PID I-Max";
const char hIMax[] CORE_PROGMEM =
    "Obergrenze des Integralanteils (Promille Heizleistung). Etwa die Leistung, die die Temperatur im "
    "Leerlauf hält.";
const char lSteamKp[] CORE_PROGMEM = "Dampf Kp";
const char hSteamKp[] CORE_PROGMEM = "Proportionalanteil im Dampfmodus (ohne I- und D-Anteil).";
const char lBdOn[] CORE_PROGMEM = "Brüh-PID";
const char hBdOn[] CORE_PROGMEM =
    "Eigene PID-Werte für die Brüh-PID-Zeit nach Erkennung eines Shots. Mit Kp 120 / Tv 20 fiel die "
    "Temperatur weniger, schwang danach aber über 4 min lang auf 98 °C über.";
const char lBdKp[] CORE_PROGMEM = "Brüh-PID Kp";
const char hBdKp[] CORE_PROGMEM = "Proportionalanteil während eines Shots (nur mit Brüh-PID).";
const char lBdTn[] CORE_PROGMEM = "Brüh-PID Tn";
const char hBdTn[] CORE_PROGMEM = "Nachstellzeit während eines Shots in s, 0 = kein I-Anteil.";
const char lBdTv[] CORE_PROGMEM = "Brüh-PID Tv";
const char hBdTv[] CORE_PROGMEM = "Vorhaltezeit während eines Shots in s.";
const char lBdTime[] CORE_PROGMEM = "Brüh-PID-Zeit";
const char hBdTime[] CORE_PROGMEM =
    "So lange nach der Shot-Erkennung gelten die Brüh-PID-Werte. Die Erkennung kommt ca. 4 s nach "
    "Shot-Beginn.";
const char lBdSens[] CORE_PROGMEM = "Erkennungs-Empfindlichkeit";
const char hBdSens[] CORE_PROGMEM =
    "Ein Shot wird erkannt, wenn die Temperatur schneller als dieser Wert (1/1000 °C pro s) fällt. "
    "Kleiner = empfindlicher, aber Gefahr von Fehlerkennungen. 0 = Erkennung aus.";

const ParamDef defs[kParamCount] = {
    // key, legacy, label, help, unit, type, min, max, step, default, slot, byte, flags
    {"pid_on", "PID_ON", lPidOn, hPidOn, "", ParamType::Bool, 0, 1, 1, 1, kSlotPidOn, true,
     kWritable | kMqtt},
    {"brew_setpoint", "BREW_SET_POINT", lBrewSp, hBrewSp, "°C", ParamType::Number, 80, 105, 0.1f, 96,
     kSlotBrewSetpoint, false, kWritable | kMqtt},
    {"steam_setpoint", "STEAM_SET_POINT", lSteamSp, hSteamSp, "°C", ParamType::Number, 100, 140, 0.5f,
     120, kSlotSteamSetpoint, false, kWritable | kMqtt},
    {"steam_on", "STEAM_MODE", lSteamOn, hSteamOn, "", ParamType::Bool, 0, 1, 1, 0, kSlotNone, false,
     kWritable | kMqtt},
    {"temp_offset", "BREW_TEMP_OFFSET", lOffset, hOffset, "°C", ParamType::Number, 0, 20, 0.1f, 0,
     kSlotBrewTempOffset, false, kWritable | kExpert},
    {"pid_kp", "PID_KP", lKp, hKp, "", ParamType::Number, 0, 200, 0.1f, 62, kSlotPidKpRegular, false,
     kWritable | kExpert | kDiagnostic},
    {"pid_tn", "PID_TN", lTn, hTn, "s", ParamType::Number, 0, 999, 0.1f, 52, kSlotPidTnRegular, false,
     kWritable | kExpert | kDiagnostic},
    {"pid_tv", "PID_TV", lTv, hTv, "s", ParamType::Number, 0, 999, 0.1f, 11.5f, kSlotPidTvRegular, false,
     kWritable | kExpert | kDiagnostic},
    {"pid_i_max", "PID_I_MAX", lIMax, hIMax, "", ParamType::Number, 0, 999, 1, 75, kSlotPidIMaxRegular,
     false, kWritable | kExpert | kDiagnostic},
    {"steam_kp", "STEAM_KP", lSteamKp, hSteamKp, "", ParamType::Number, 0, 500, 1, 150, kSlotPidKpSteam,
     false, kWritable | kExpert},
    {"bd_pid_on", "PID_BD_ON", lBdOn, hBdOn, "", ParamType::Bool, 0, 1, 1, 0, kSlotUseBdPid, true,
     kWritable | kExpert},
    {"bd_kp", "PID_BD_KP", lBdKp, hBdKp, "", ParamType::Number, 0, 200, 0.1f, 50, kSlotPidKpBd, false,
     kWritable | kExpert},
    {"bd_tn", "PID_BD_TN", lBdTn, hBdTn, "s", ParamType::Number, 0, 999, 0.1f, 0, kSlotPidTnBd, false,
     kWritable | kExpert},
    {"bd_tv", "PID_BD_TV", lBdTv, hBdTv, "s", ParamType::Number, 0, 999, 0.1f, 20, kSlotPidTvBd, false,
     kWritable | kExpert},
    {"bd_time", "PID_BD_TIME", lBdTime, hBdTime, "s", ParamType::Number, 1, 180, 1, 30, kSlotBrewSwTime,
     false, kWritable | kExpert},
    {"bd_sensitivity", "PID_BD_BREWSENSITIVITY", lBdSens, hBdSens, "", ParamType::Number, 0, 999, 1, 120,
     kSlotBdThreshold, false, kWritable | kExpert},
};

const char *const readOnlyKeys[] = {"temperature", "TEMP", "version", "VERSION", "state", "heater",
                                    "ready", "error"};

}  // namespace

const ParamDef &paramDef(ParamId id) { return defs[id]; }

bool isReadOnlyKey(const char *key) {
    for (const char *k : readOnlyKeys) {
        if (strcmp(k, key) == 0) return true;
    }
    return false;
}

ParamId findParam(const char *key) {
    for (int i = 0; i < kParamCount; i++) {
        if (strcmp(defs[i].key, key) == 0 || strcmp(defs[i].legacyKey, key) == 0) return (ParamId)i;
    }
    return kParamCount;
}

const char *paramErrorCode(ParamError error) {
    switch (error) {
        case ParamError::None: return "ok";
        case ParamError::Unknown: return "unknown";
        case ParamError::ReadOnly: return "read_only";
        case ParamError::Invalid: return "invalid";
        case ParamError::OutOfRange: return "out_of_range";
    }
    return "invalid";
}

ParamError checkParamValue(ParamId id, double value) {
    if (id >= kParamCount) return ParamError::Unknown;
    const ParamDef &d = defs[id];
    if (!(d.flags & kWritable)) return ParamError::ReadOnly;
    if (value - value != 0) return ParamError::Invalid;  // NAN or infinite
    if (d.type == ParamType::Bool && value != 0 && value != 1) return ParamError::Invalid;
    // limits are floats: compare with a little slack so 11.5 or 0.1 steps are not rejected by rounding
    if (value < d.min - 1e-6 || value > d.max + 1e-6) return ParamError::OutOfRange;
    return ParamError::None;
}

static bool equalsIgnoreCase(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

ParamError parseParamValue(ParamId id, const char *text, double &value) {
    if (id >= kParamCount) return ParamError::Unknown;
    if (!(defs[id].flags & kWritable)) return ParamError::ReadOnly;

    while (*text == ' ') text++;
    char trimmed[16];
    size_t n = strlen(text);
    while (n > 0 && text[n - 1] == ' ') n--;
    if (n == 0 || n >= sizeof(trimmed)) return ParamError::Invalid;
    memcpy(trimmed, text, n);
    trimmed[n] = '\0';

    double parsed;
    if (defs[id].type == ParamType::Bool &&
        (equalsIgnoreCase(trimmed, "true") || equalsIgnoreCase(trimmed, "on"))) {
        parsed = 1;
    } else if (defs[id].type == ParamType::Bool &&
               (equalsIgnoreCase(trimmed, "false") || equalsIgnoreCase(trimmed, "off"))) {
        parsed = 0;
    } else {
        char *end;
        parsed = strtod(trimmed, &end);
        if (*end != '\0' || end == trimmed) return ParamError::Invalid;
    }

    ParamError error = checkParamValue(id, parsed);
    if (error == ParamError::None) value = parsed;
    return error;
}

ParamSet::ParamSet() {
    for (int i = 0; i < kParamCount; i++) values[i] = defs[i].def;
}

void ParamSet::load(ParamStore &store) {
    for (int i = 0; i < kParamCount; i++) {
        const ParamDef &d = defs[i];
        if (d.slot == kSlotNone) continue;
        double stored;
        if (store.load(d.slot, d.slotIsByte, stored) && checkParamValue((ParamId)i, stored) == ParamError::None) {
            values[i] = stored;
        }
    }
}

int ParamSet::apply(const Write *writes, size_t count, ParamStore *store) {
    int changed = 0;
    bool persist = false;
    for (size_t i = 0; i < count; i++) {
        const Write &w = writes[i];
        if (values[w.id] == w.value) continue;
        values[w.id] = w.value;
        changed++;
        const ParamDef &d = defs[w.id];
        if (store && d.slot != kSlotNone) {
            store->save(d.slot, d.slotIsByte, w.value);
            persist = true;
        }
    }
    if (persist) store->commit();
    return changed;
}

}  // namespace silvia
