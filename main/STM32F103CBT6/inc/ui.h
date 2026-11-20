#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "json_frame.h"

void ui_init(void);
void ui_show_hot(const json_frame_t *frame);
void ui_show_stage(const char *stage, int32_t progress);
void ui_set_link(uint8_t connected);
void ui_set_battery(uint32_t millivolts);
void ui_refresh_all(void);
void ui_test(void);

#endif
