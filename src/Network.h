/**
 * @file Network.h
 * @brief WiFi that keeps trying forever in the background, OTA updates and the clock.
 *
 * Nothing here blocks the control loop: no waiting for a connection, no attempt limit, and
 * WiFi.disconnect() is never called (it can clear the station config). Offline only means
 * "no network services"; the machine heats normally.
 */
#pragma once

#include <stdint.h>

namespace network {

struct Callbacks {
    void (*otaStart)();  // stop the heater, OTA writes to flash
    void (*otaEnd)();    // also called on OTA errors
};

void begin(const char *hostname, const char *ssid, const char *password, const char *otaPassword,
           const Callbacks &callbacks);
void loop(uint32_t nowMs);

bool connected();
int rssi();
/** seconds since 1970, 0 until NTP answered */
uint32_t unixTime();

}  // namespace network
