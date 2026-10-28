/**
 * @file Params.h
 * @brief The single parameter table: every setting with its limits, storage slot, visibility
 *        and writability. Web, API, MQTT and storage are all driven by it.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace silvia {

/**
 * Storage slots of the 3.3.0 EEPROM layout (sto_item_id_t in src/Storage.h, same order).
 * Kept so stored values survive flashing the fork.
 */
enum StorageSlot : int8_t {
    kSlotNone = -1,
    kSlotPidKpRegular = 0,
    kSlotPidTnRegular = 1,
    kSlotPidTvRegular = 2,
    kSlotBrewSetpoint = 3,
    kSlotPidKpBd = 9,
    kSlotPidTnBd = 10,
    kSlotPidTvBd = 11,
    kSlotBrewSwTime = 12,
    kSlotBdThreshold = 13,
    kSlotPidOn = 19,
    kSlotPidKpSteam = 21,
    kSlotPidIMaxRegular = 22,
    kSlotBrewTempOffset = 23,
    kSlotUseBdPid = 25,
    kSlotSteamSetpoint = 26,
    kSlotStandbyMin = 50,  // new in the fork: a byte in a free area of the 3.3.0 layout
};

enum class ParamType : uint8_t { Number, Bool };

enum ParamFlags : uint8_t {
    kWritable = 1 << 0,  // can be changed through web/API/MQTT
    kExpert = 1 << 1,    // shown in the collapsed expert section of the web UI
    kMqtt = 1 << 2,      // exposed as a Home Assistant control entity
    kDiagnostic = 1 << 3, // exposed to Home Assistant as a (hidden) diagnostic entity
    kInteger = 1 << 4     // whole numbers only
};

enum ParamId : uint8_t {
    kPidOn,
    kBrewSetpoint,
    kSteamSetpoint,
    kSteamOn,
    kTempOffset,
    kPidKp,
    kPidTn,
    kPidTv,
    kPidIMax,
    kSteamKp,
    kBdPidOn,
    kBdKp,
    kBdTn,
    kBdTv,
    kBdTime,
    kBdSensitivity,
    kStandbyMin,
    kParamCount
};

struct ParamDef {
    const char *key;        // API/MQTT name
    const char *legacyKey;  // 3.3.0 name, accepted on input
    const char *label;      // German, flash resident on the ESP8266
    const char *help;       // German, flash resident on the ESP8266
    const char *unit;
    ParamType type;
    float min;
    float max;
    float step;
    float def;
    StorageSlot slot;       // kSlotNone = runtime only, not persisted
    bool slotIsByte;        // stored as uint8_t instead of double
    uint8_t flags;
};

const ParamDef &paramDef(ParamId id);

/** read-only values that callers may post back (e.g. a stale form); always rejected */
bool isReadOnlyKey(const char *key);

/** @return kParamCount if the key is unknown (accepts the 3.3.0 names too) */
ParamId findParam(const char *key);

enum class ParamError : uint8_t { None, Unknown, ReadOnly, Invalid, OutOfRange };

const char *paramErrorCode(ParamError error);

/**
 * @brief parse a text value (form field, MQTT payload, JSON literal) for a parameter
 *
 * Numbers must be complete ("12abc" and "" are invalid), booleans accept 0/1, true/false
 * and ON/OFF. Limits are inclusive.
 */
ParamError parseParamValue(ParamId id, const char *text, double &value);

/** check an already numeric value (e.g. from a JSON body) */
ParamError checkParamValue(ParamId id, double value);

/** backend for persisted parameters; the ESP8266 adapter wraps the 3.3.0 EEPROM layout */
class ParamStore {
  public:
    virtual ~ParamStore() {}
    /** @return false if the slot is empty or unreadable */
    virtual bool load(StorageSlot slot, bool isByte, double &value) = 0;
    virtual void save(StorageSlot slot, bool isByte, double value) = 0;
    virtual bool commit() = 0;
};

/**
 * @brief current values of all parameters
 *
 * Writes are all-or-nothing: a batch with one invalid field changes nothing. Only values
 * that actually changed are written to the store, and the store is committed once per batch.
 */
class ParamSet {
  public:
    ParamSet();

    /** load persisted values; empty or out-of-range slots keep the default */
    void load(ParamStore &store);

    double get(ParamId id) const { return values[id]; }
    bool flag(ParamId id) const { return values[id] != 0; }

    struct Write {
        ParamId id;
        double value;
    };

    /**
     * @brief apply a validated batch and persist changed values
     * @return number of values that changed
     */
    int apply(const Write *writes, size_t count, ParamStore *store);

    /** set a runtime value without validation or persistence (used by the core itself) */
    void setRuntime(ParamId id, double value) { values[id] = value; }

  private:
    double values[kParamCount];
};

}  // namespace silvia
