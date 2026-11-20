#ifndef INA219_H
#define INA219_H

#include <stdint.h>

void ina219_init(void);
uint8_t ina219_read(uint32_t *bus_mv, int32_t *current_ma, uint32_t *power_mw);

#endif
