/**
 * @file    userConfig.h
 * @brief   Build configuration of the Silvia-only fork
 *
 * Generated from tools/flash/userConfig.silvia.h by tools/flash/flash-silvia.sh, which fills
 * in the @...@ placeholders at build time and puts them back afterwards. Never commit
 * src/userConfig.h with real values.
 *
 * Modified 2026 for the Silvia-only fork: options for other machines, boards and
 * peripherals removed.
 */
#ifndef _userConfig_H
#define _userConfig_H

// firmware version (must match with definitions in the main source file)
#define USR_FW_VERSION    3
#define USR_FW_SUBVERSION 3
#define USR_FW_HOTFIX     0
#define USR_FW_BRANCH     "SILVIA-1"

// Display (SSD1306 128x64 on I2C)
#define OLED_I2C 0x3C              // I2C address for OLED, 0x3C by default
#define DISPLAYTEMPLATE 3          // only temperature
#define DISPLAYROTATE U8G2_R0      // no rotation
#define SHOTTIMER 1                // 1 = show shot timer
#define HEATINGLOGO 0              // 0 = deactivated
#define OFFLINEGLOGO 1             // 1 = show logo while PID is off
#define BREWSWITCHDELAY 3000       // time in ms the shot timer is held after the shot
#define VERBOSE 0                  // 1 = verbose serial output
#define LANGUAGE 0                 // 0 = German

// Connectivity
#define HOSTNAME "silvia"
#define WIFI_SSID "@WIFI_SSID@"     // filled in by tools/flash/flash-silvia.sh
#define WIFI_PASS "@WIFI_PASS@"     // filled in by tools/flash/flash-silvia.sh, never commit
#define MAXWIFIRECONNECTS 5        // maximum number of reconnection attempts during boot
#define WIFICONNECTIONDELAY 10000  // delay between reconnects in ms

// Brew detection
#define BREWDETECTION 1            // software brew detection (temperature drop)
#define BREWPID_DELAY 0            // 3.3.0 does not stop heating during this delay, it freezes the PID output at its last value

// PlatformIO OTA
#define OTA true                   // true = OTA activated
#define OTAHOST "silvia"           // Name to be shown in PlatformIO port
#define OTAPASS "@OTA_PASS@"        // filled in by tools/flash/flash-silvia.sh

// MQTT (off until the Home Assistant step)
#define MQTT 0
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_TOPIC_PREFIX "custom/"
#define MQTT_SERVER_IP "0.0.0.0"
#define MQTT_SERVER_PORT 1883

// PID
#define EMA_FACTOR 0.6             // Smoothing of input that is used for Tv (derivative component of PID)

#endif
