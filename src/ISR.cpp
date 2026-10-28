// Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
#include <Arduino.h>
#include "ISR.h"
#include "hardware.h"

unsigned int isrCounter = 0;  // counter for ISR
unsigned long windowStartTime;
unsigned int windowSize = 1000;
bool skipHeaterISR = false;

#if defined(ESP8266)
void IRAM_ATTR onTimer1ISR() {
    timer1_write(6250); // set interrupt time to 20ms

    if (skipHeaterISR) return;

    if (pidOutput <= isrCounter) {
        digitalWrite(PINHEATER, LOW);
    } else {
        digitalWrite(PINHEATER, HIGH);
    }

    isrCounter += 20; // += 20 because one tick = 20ms

    //set PID output as relais commands
    if (isrCounter >= windowSize) {
        isrCounter = 0;
    }
}
#endif


/**
 * @brief Initialize hardware timers
 */
void initTimer1(void) {
    #if defined(ESP8266)
        /* Timer1 ISR - Initialisierung
         * TIM_DIV1 = 0,   80MHz (80 ticks/us - 104857.588 us max)
         * TIM_DIV16 = 1,  5MHz (5 ticks/us - 1677721.4 us max)
         * TIM_DIV256 = 3  312.5Khz (1 tick = 3.2us - 26843542.4 us max)
         */
        timer1_isr_init();
        timer1_attachInterrupt(onTimer1ISR);
        timer1_write(6250); // DIV256: set interrupt time to 20ms
    #endif
}

void enableTimer1(void) {
    #if defined(ESP8266)
        timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
    #endif
}

void disableTimer1(void) {
    #if defined(ESP8266)
        timer1_disable();
    #endif
}

bool isTimer1Enabled(void) {
    bool timerEnabled = false;

    #if defined(ESP8266)
        timerEnabled = ((T1C & (1 << TCTE)) != 0);
    #endif

    return timerEnabled;
}