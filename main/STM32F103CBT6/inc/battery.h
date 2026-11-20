#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

void battery_init(void);
uint16_t battery_read_raw(void);
uint32_t battery_read_millivolts(void);
uint8_t battery_percent(uint32_t millivolts);
uint8_t battery_is_undervoltage(uint32_t millivolts);

#endif
