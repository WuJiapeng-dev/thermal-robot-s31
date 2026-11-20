#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

void encoder_init(void);
void encoder_update(void);
void encoder_reset(void);
int32_t encoder_left_count(void);
int32_t encoder_right_count(void);
void encoder_test(void);

#endif
