#include "Network.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <time.h>

#include "Backoff.h"
#include "debugSerial.h"

namespace network {

namespace {
const char *ssid;
const char *password;
Callbacks callbacks;
silvia::Backoff backoff(10000, 60000);  // give an association + DHCP time before retrying
bool otaStarted = false;
bool wasConnected = false;
}  // namespace

void begin(const char *hostname, const char *wifiSsid, const char *wifiPassword, const char *otaPassword,
           const Callbacks &cb) {
    ssid = wifiSsid;
    password = wifiPassword;
    callbacks = cb;

    WiFi.persistent(false);  // keep flash writes and stored credentials out of it
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    backoff.due(millis());  // counts as the first attempt

    configTime(0, 0, "pool.ntp.org", "time.google.com");

    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(otaPassword);
    ArduinoOTA.onStart([]() { callbacks.otaStart(); });
    ArduinoOTA.onEnd([]() { callbacks.otaEnd(); });
    ArduinoOTA.onError([](ota_error_t) { callbacks.otaEnd(); });
}

void loop(uint32_t nowMs) {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wasConnected) {
            debugPrintf("WiFi connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            wasConnected = true;
        }
        backoff.reset();
        if (!otaStarted) {
            ArduinoOTA.begin();  // needs the station up for mDNS
            otaStarted = true;
        }
        ArduinoOTA.handle();
        return;
    }

    if (wasConnected) {
        debugPrintln("WiFi lost, reconnecting in the background");
        wasConnected = false;
    }
    if (backoff.due(nowMs)) {
        debugPrintf("WiFi attempt %u\n", backoff.attemptCount());
        WiFi.begin(ssid, password);  // no disconnect(): it may wipe the station config
    }
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

int rssi() { return connected() ? WiFi.RSSI() : 0; }

uint32_t unixTime() {
    time_t now = time(nullptr);
    return now > 1700000000 ? (uint32_t)now : 0;
}

}  // namespace network
