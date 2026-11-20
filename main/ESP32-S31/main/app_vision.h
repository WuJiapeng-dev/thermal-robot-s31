#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool sccb_ack;
    uint8_t pid;
    uint8_t ver;
} camera_probe_result_t;

esp_err_t app_vision_init(void);
esp_err_t app_vision_probe(camera_probe_result_t *result);
esp_err_t app_vision_self_test(void);
void app_vision_report_status(void);

#ifdef __cplusplus
}
#endif
