#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

void board_init(void);
void board_background_task(void);
void board_buzzer_beep(uint16_t duration_ms);

#endif
