/**
 * @file    userConfig.h
 * @brief   Build configuration of the Silvia-only fork: only the secrets and names
 *
 * Generated from tools/flash/userConfig.silvia.h by tools/flash/flash-silvia.sh, which fills
 * in the @...@ placeholders at build time and puts them back afterwards. Never commit
 * src/userConfig.h with real values. Everything else is fixed in the code or in the
 * parameter table (lib/silvia_core/src/Params.cpp).
 */
#ifndef _userConfig_H
#define _userConfig_H

#define HOSTNAME "silvia"

// WiFi
#define WIFI_SSID "@WIFI_SSID@"
#define WIFI_PASS "@WIFI_PASS@"

// OTA updates (espota)
#define OTAPASS "@OTA_PASS@"

// HTTP basic auth for every write (web UI, API)
#define WEB_USER "silvia"
#define WEB_PASS "@WEB_PASS@"

#endif
