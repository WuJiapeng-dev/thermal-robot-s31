#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

void timebase_init(void);
void timebase_systick_irq(void);
uint32_t timebase_millis(void);
void timebase_delay_ms(uint32_t ms);
uint8_t timebase_expired(uint32_t now, uint32_t start, uint32_t timeout_ms);

#endif
