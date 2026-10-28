/**
 * @file MqttLink.h
 * @brief MQTT connection to the Home Assistant Mosquitto broker (user "silvia"). Topics,
 *        payloads, cadence and command validation are in the core (lib/silvia_core/src/Mqtt.h).
 *
 * Reconnects with the same endless backoff as WiFi. PubSubClient connects synchronously; the
 * TCP connect is limited to 500 ms, so a missing broker costs at most that once per backoff
 * step (the heater ISR keeps its duty meanwhile). Use an IP address, not a hostname.
 */
#pragma once

#include <stdint.h>

#include "Json.h"
#include "SilviaCore.h"

namespace mqttlink {

void begin(silvia::SilviaCore *core, const char *host, uint16_t port, const char *user, const char *password);
void loop(uint32_t nowMs, const silvia::DeviceInfo &device);
void publish(const silvia::Event &event);

/** false if no broker is configured */
bool enabled();
bool connected();

}  // namespace mqttlink
