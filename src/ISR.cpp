// Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
#include <Arduino.h>
#include "ISR.h"
#include "hardware.h"

volatile uint16_t heaterDuty = 0;
bool skipHeaterISR = false;

static unsigned int isrCounter = 0;  // ms into the current 1 s window

void IRAM_ATTR onTimer1ISR() {
    timer1_write(6250); // set interrupt time to 20ms

    if (skipHeaterISR) return;

    digitalWrite(PINHEATER, heaterDuty > isrCounter ? HIGH : LOW);

    isrCounter += 20; // += 20 because one tick = 20ms
    if (isrCounter >= 1000) {
        isrCounter = 0;
    }
}

/**
 * @brief Initialize hardware timers
 */
void initTimer1(void) {
    /* Timer1 ISR - Initialisierung
     * TIM_DIV256 = 3  312.5Khz (1 tick = 3.2us - 26843542.4 us max)
     */
    timer1_isr_init();
    timer1_attachInterrupt(onTimer1ISR);
    timer1_write(6250); // DIV256: set interrupt time to 20ms
}

void enableTimer1(void) {
    timer1_enable(TIM_DIV256, TIM_EDGE, TIM_SINGLE);
}

void disableTimer1(void) {
    timer1_disable();
}
