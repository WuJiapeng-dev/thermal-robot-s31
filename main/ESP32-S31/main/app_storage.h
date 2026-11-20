#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_storage_init(void);
bool app_storage_mounted(void);
esp_err_t app_storage_self_test(void);
esp_err_t app_storage_ensure_directories(void);
esp_err_t app_storage_logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
esp_err_t app_storage_append_scan_json(const char *json);
esp_err_t app_storage_write_thermal_snapshot(const uint16_t *pixels, uint16_t width, uint16_t height,
                                             uint32_t frame_id, uint32_t fpa_raw);
/* Streams a 24-bit bottom-up BMP; no full-image RAM buffer is allocated. */
esp_err_t app_storage_write_thermal_pseudo_bmp(const uint16_t *pixels, uint16_t width, uint16_t height,
                                               uint32_t frame_id, char *path_out, size_t path_capacity);
esp_err_t app_storage_read_file(const char *path, uint8_t *buffer, size_t capacity, size_t *length);
esp_err_t app_storage_write_file(const char *path, const uint8_t *buffer, size_t length);

#ifdef __cplusplus
}
#endif
