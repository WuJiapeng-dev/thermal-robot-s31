#ifndef MOTION_H
#define MOTION_H

#include <stdint.h>

void motion_init(void);
uint8_t motion_move_mm(int32_t distance_mm);
uint8_t motion_turn_deg(int32_t angle_deg);
uint8_t motion_step_scan(int32_t step_mm, uint32_t dwell_ms);
void motion_stop(void);

#endif
