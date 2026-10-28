/**
 * @file main.cpp
 *
 * @brief Main sketch: a thin shell around the hardware-free core (lib/silvia_core).
 *
 * Every 100 ms: read the sensor (every 400 ms), tick the core, hand the heater duty to the ISR
 * right away, and only then do the slow things (network, display over I2C).
 *
 * Based on rancilio-pid.cpp of Clever Coffee 3.3.0.
 * Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
 */

#include <Arduino.h>
#include <ZACwire.h>

#include "DisplayModel.h"
#include "DisplayRenderer.h"
#include "EepromStore.h"
#include "History.h"
#include "MqttLink.h"
#include "ISR.h"
#include "Network.h"
#include "SilviaCore.h"
#include "Version.h"
#include "WebApi.h"
#include "debugSerial.h"
#include "hardware.h"
#include "userConfig.h"

namespace {

const uint32_t kTickMs = 100;
const uint32_t kSensorMs = 400;  // TSIC306 update rate used by 3.3.0 and the brew detection

ZACwire tsic(PINTEMPSENSOR, 306);
EepromStore store;
silvia::SilviaCore core(&store);
silvia::DisplayModel displayModel;
silvia::History history;

uint32_t lastTickMs = 0;
uint32_t lastSensorMs = 0;
uint32_t lastDisplayMs = 0;

void heaterOff() {
    heaterDuty = 0;
    digitalWrite(PINHEATER, LOW);
}

silvia::DeviceInfo deviceInfo() {
    return {SILVIA_VERSION, network::rssi(), network::connected(), mqttlink::connected(), ESP.getFreeHeap()};
}

void logEvent(const silvia::Event &e) {
    switch (e.type) {
        case silvia::EventType::ShotStarted:
            debugPrintf("shot %u started (back-dated %.1f s)\n", e.shot.id, (e.atMs - e.shot.startMs) / 1000.0);
            break;
        case silvia::EventType::ShotFinished:
            debugPrintf("shot %u: %.1f s, %.1f -> %.1f C\n", e.shot.id, e.shot.durationS, e.shot.startTemp,
                        e.shot.minTemp);
            break;
        case silvia::EventType::ShotComplete:
            debugPrintf("shot %u complete: peak %.1f C\n", e.shot.id, e.shot.peakTemp);
            break;
        case silvia::EventType::ErrorRaised:
            debugPrintf("ERROR %s, heater off\n", silvia::errorName(e.error));
            break;
        case silvia::EventType::ErrorCleared:
            debugPrintf("error %s cleared\n", silvia::errorName(e.error));
            break;
        case silvia::EventType::ReadyReached:
            debugPrintf("ready%s\n", e.firstSinceBoot ? " (heat-up done)" : "");
            break;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(PINHEATER, OUTPUT);
    heaterOff();

    display::begin();
    display::bootMessage("Silvia", SILVIA_VERSION);

    store.begin();
    core.begin(millis());

    network::begin(HOSTNAME, WIFI_SSID, WIFI_PASS, OTAPASS,
                   {[]() {  // OTA start: flash is written, no heater switching meanwhile
                        disableTimer1();
                        heaterOff();
                        display::bootMessage("Update", "Heizung aus");
                    },
                    []() { enableTimer1(); }});
    webapi::begin({&core, &history, WEB_USER, WEB_PASS, deviceInfo});
    mqttlink::begin(&core, MQTT_HOST, MQTT_PORT, MQTT_USER, MQTT_PASS);

    initTimer1();
    enableTimer1();
    debugPrintf("Silvia %s started\n", SILVIA_VERSION);
}

void loop() {
    uint32_t now = millis();

    if (now - lastTickMs >= kTickMs) {
        lastTickMs = now;
        silvia::TickInput in = {now, false, 0, network::unixTime()};
        if (now - lastSensorMs >= kSensorMs) {
            lastSensorMs = now;
            in.hasReading = true;
            in.raw = tsic.getTemp();
        }

        // heater decision first: nothing below may delay switching it off
        heaterDuty = core.tick(in);
        if (heaterDuty == 0 && !skipHeaterISR) digitalWrite(PINHEATER, LOW);

        history.update(core.snapshot());
        silvia::Event e;
        while (core.popEvent(e)) {
            logEvent(e);
            history.onEvent(e);
            webapi::publish(e);
            mqttlink::publish(e);
        }
    }

    network::loop(now);
    webapi::loop(now);
    mqttlink::loop(now, deviceInfo());

    const silvia::Snapshot &s = core.snapshot();
    uint32_t displayInterval = s.state == silvia::State::Shot ? 200 : 500;
    if (now - lastDisplayMs >= displayInterval) {
        lastDisplayMs = now;
        display::draw(displayModel.update(s, now, network::connected(), mqttlink::enabled(), mqttlink::connected()), now);
    }
}
