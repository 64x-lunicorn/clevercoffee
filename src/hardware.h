/**
 * @file hardware.h
 *
 * @brief Pin mapping of the NodeMCU (ESP8266) on the Clever Coffee PCB rev 1.2
 *
 * Modified 2026 for the Silvia-only fork: only the pins the Silvia E uses.
 */
#pragma once

#define PINTEMPSENSOR 2            // TSIC306 signal
#define PINHEATER 14               // SSR for the boiler heater
#define OLED_SCL 5                 // display clock
#define OLED_SDA 4                 // display data
