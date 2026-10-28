/**
 * @file Json.h
 * @brief JSON documents of the HTTP API, written without allocations into a caller buffer.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SilviaCore.h"

namespace silvia {

class JsonOut {
  public:
    JsonOut(char *buffer, size_t capacity);

    JsonOut &beginObject();
    JsonOut &endObject();
    JsonOut &beginArray();
    JsonOut &endArray();
    JsonOut &key(const char *name);
    JsonOut &str(const char *value);
    JsonOut &strP(const char *flashValue);  // string in ESP8266 flash (PROGMEM)
    JsonOut &num(double value, int decimals);  // NAN/inf become null
    JsonOut &integer(long value);
    JsonOut &boolean(bool value);
    JsonOut &null();
    JsonOut &raw(const char *text);

    const char *c_str() const { return buf; }
    size_t length() const { return len; }
    bool overflow() const { return over; }

  private:
    void put(char c);
    void puts(const char *s);
    void separator();
    void escaped(char c);

    char *buf;
    size_t cap;
    size_t len = 0;
    bool over = false;
    bool needComma = false;
};

/** what only the device knows */
struct DeviceInfo {
    const char *version;
    int wifiRssi;          // dBm, 0 if not connected
    bool wifiConnected;
    bool mqttConnected;
    uint32_t freeHeap;
};

void writeShot(JsonOut &out, const ShotSummary &shot);

/** GET /api/state */
void writeState(JsonOut &out, const Snapshot &s, const DeviceInfo &device);

/** GET /api/parameters */
void writeParameters(JsonOut &out, const ParamSet &params);

/** 400 body of POST /api/parameters and /api/command */
void writeFieldErrors(JsonOut &out, const FieldError *errors, size_t count);

}  // namespace silvia
