#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *pixels; /* Caller-provided storage for THERMAL_PIXEL_COUNT uint16 values. */
    uint16_t width;
    uint16_t height;
    uint32_t frame_id;
    uint32_t fpa_raw;
    uint64_t timestamp_ms;
} thermal_frame_t;

esp_err_t app_thermal_init(void);
esp_err_t thermal_get_frame(thermal_frame_t *frame, uint32_t timeout_ms);
esp_err_t app_thermal_get_latest(thermal_frame_t *frame);
float raw_to_celsius(uint16_t raw);
esp_err_t app_thermal_set_shutter(bool open);
esp_err_t app_thermal_download_calibration(void);
const uint8_t *app_thermal_calibration_data(size_t *length);
esp_err_t app_thermal_self_test(void);
esp_err_t app_thermal_usb_probe(void);
bool app_thermal_connected(void);
uint32_t app_thermal_received_frames(void);
uint32_t app_thermal_dropped_frames(void);
uint32_t app_thermal_failed_frames(void);

#ifdef __cplusplus
}
#endif
