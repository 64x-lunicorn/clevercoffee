/**
 * @file CorePlatform.h
 * @brief Keeps the core free of Arduino headers: flash-resident strings on the ESP8266,
 *        plain pointers on the host.
 */
#pragma once

#if defined(ARDUINO_ARCH_ESP8266)
    #include <pgmspace.h>
    #define CORE_PROGMEM PROGMEM
    #define CORE_READ_BYTE(p) pgm_read_byte(p)
#else
    #define CORE_PROGMEM
    #define CORE_READ_BYTE(p) (*(const char *)(p))
#endif
