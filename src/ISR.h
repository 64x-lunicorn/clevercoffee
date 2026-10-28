/**
 * @file ISR.h
 *
 * @brief Timer - ISR for PID calculation and heat realay output
 *
 * Modified 2026 for the Silvia-only fork (github.com/64x-lunicorn/clevercoffee).
 */

#ifndef _ISR_H_
#define _ISR_H_

extern unsigned long windowStartTime;
extern double pidOutput; 
extern unsigned int isrCounter;
extern unsigned int windowSize;
extern bool skipHeaterISR;

/**
 * @brief pauses heater switching in the ISR for the current scope (flash access in web handlers)
 *
 * Restores the previous state on every return path. Setting skipHeaterISR by hand left it
 * true after a successfully served HTML page, freezing the heater pin until the next request.
 */
class HeaterIsrPause {
  public:
    HeaterIsrPause() : previous(skipHeaterISR) { skipHeaterISR = true; }
    ~HeaterIsrPause() { skipHeaterISR = previous; }
    HeaterIsrPause(const HeaterIsrPause &) = delete;
    HeaterIsrPause &operator=(const HeaterIsrPause &) = delete;

  private:
    bool previous;
};

void initTimer1(void);
void enableTimer1(void);
void disableTimer1(void);
bool isTimer1Enabled(void);

#endif
