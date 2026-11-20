#ifndef COMM_H
#define COMM_H

#include <stdint.h>

void comm_init(void);
void comm_usart2_irq(void);
uint8_t comm_frame_available(void);
const char *comm_acquire_frame(uint16_t *length);
void comm_release_frame(void);
void comm_send_ready(void);
void comm_send_ack(int32_t id);
void comm_send_capture(uint32_t sequence, uint32_t frames);
void comm_mark_capture_done(uint32_t sequence, int32_t captured_frames);
uint8_t comm_capture_completed(uint32_t sequence);
int32_t comm_capture_frames(void);
uint32_t comm_oversize_count(void);
uint32_t comm_drop_count(void);

#endif
