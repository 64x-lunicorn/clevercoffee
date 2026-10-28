// library headers first: Arduino.h's round() macro breaks <random> if it comes before them
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "Backoff.h"
#include "Mqtt.h"
#include "MqttLink.h"
#include "debugSerial.h"

using namespace silvia;

namespace mqttlink {

namespace {

WiFiClient net;
PubSubClient client(net);
SilviaCore *core = nullptr;
const char *user;
const char *password;
bool isEnabled = false;
Backoff backoff;
mqtt::Cadence cadence;
bool announce = false;          // (re)publish discovery: after connecting and on the HA birth message
char lastSettings[256] = "";    // settings are published when they change
uint32_t lastSettingsCheckMs = 0;
char buf[1100];

void onMessage(char *topic, byte *payload, unsigned int length) {
    char text[32];
    size_t n = length < sizeof text - 1 ? length : sizeof text - 1;
    memcpy(text, payload, n);
    text[n] = '\0';

    if (strcmp(topic, mqtt::kHaStatusTopic) == 0) {
        if (strcmp(text, "online") == 0) announce = true;  // HA restarted: entities need discovery again
        return;
    }
    ParamError error = mqtt::handleCommand(*core, topic, text);
    if (error != ParamError::None) {
        debugPrintf("MQTT %s = '%s' rejected: %s\n", topic, text, paramErrorCode(error));
    }
    lastSettingsCheckMs = 0;  // echo the (possibly unchanged) settings soon
    lastSettings[0] = '\0';
}

void writeSettingsDoc(JsonOut &out) { mqtt::writeSettings(out, core->params()); }

}  // namespace

void begin(SilviaCore *c, const char *host, uint16_t port, const char *u, const char *p) {
    core = c;
    user = u;
    password = p;
    isEnabled = host && *host && *host != '@';
    if (!isEnabled) return;
    net.setTimeout(500);
    client.setServer(host, port);
    client.setBufferSize(sizeof buf);
    client.setKeepAlive(15);
    client.setSocketTimeout(2);
    client.setCallback(onMessage);
}

bool enabled() { return isEnabled; }

bool connected() { return isEnabled && client.connected(); }

void loop(uint32_t nowMs, const DeviceInfo &device) {
    if (!isEnabled || WiFi.status() != WL_CONNECTED) return;

    if (!client.connected()) {
        if (!backoff.due(nowMs)) return;
        debugPrintf("MQTT connect attempt %u\n", backoff.attemptCount());
        if (!client.connect("silvia", user, password, mqtt::kAvailabilityTopic, 1, true, "offline")) return;
        backoff.reset();
        client.publish(mqtt::kAvailabilityTopic, "online", true);
        client.subscribe(mqtt::kCommandFilter);
        client.subscribe(mqtt::kHaStatusTopic);
        announce = true;
        cadence.reset();
        lastSettings[0] = '\0';
    }

    client.loop();

    if (announce) {
        announce = false;
        char topic[96];
        char url[40];
        snprintf(url, sizeof url, "http://%s/", WiFi.localIP().toString().c_str());
        for (int i = 0; i < mqtt::discoveryCount(); i++) {
            JsonOut out(buf, sizeof buf);
            if (mqtt::discovery(i, device.version, url, topic, sizeof topic, out) && !out.overflow()) {
                client.publish(topic, out.c_str(), true);
            }
            client.loop();
        }
        lastSettings[0] = '\0';
    }

    if (cadence.due(nowMs, core->snapshot())) {
        JsonOut out(buf, sizeof buf);
        mqtt::writeState(out, core->snapshot(), device);
        if (!out.overflow()) client.publish(mqtt::kStateTopic, out.c_str(), true);
    }

    if (nowMs - lastSettingsCheckMs >= 1000 || lastSettings[0] == '\0') {
        lastSettingsCheckMs = nowMs;
        JsonOut out(buf, sizeof buf);
        writeSettingsDoc(out);
        if (!out.overflow() && strcmp(out.c_str(), lastSettings) != 0 &&
            client.publish(mqtt::kSettingsTopic, out.c_str(), true)) {
            strncpy(lastSettings, out.c_str(), sizeof lastSettings - 1);
            lastSettings[sizeof lastSettings - 1] = '\0';
        }
    }
}

void publish(const Event &event) {
    if (!connected() || event.type != EventType::ShotFinished) return;
    JsonOut out(buf, sizeof buf);
    mqtt::writeShotEvent(out, event.shot);
    if (!out.overflow()) client.publish(mqtt::kShotEventTopic, out.c_str(), false);
}

}  // namespace mqttlink
