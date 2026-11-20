#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <stdint.h>
#include <stdio.h>

void debug_console_init(void);
void debug_console_poll(void);
void debug_write_byte(uint8_t byte);
void link_history_push(const char *frame, uint16_t length);
void link_history_print(void);

#endif
