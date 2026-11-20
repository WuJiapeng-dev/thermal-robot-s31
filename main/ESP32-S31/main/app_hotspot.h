#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    float temp_c;
    float delta_c;
    uint32_t raw_delta;
    uint8_t area_px;
    float conf;
} hotspot_t;

typedef struct {
    hotspot_t items[HOTSPOT_MAX_RESULTS];
    uint8_t count;
    uint16_t median_raw;
    uint32_t candidate_regions;
} hotspot_result_t;

esp_err_t app_hotspot_init(void);
void app_hotspot_reset(void);
esp_err_t app_hotspot_analyze(const uint16_t *pixels, uint16_t width, uint16_t height,
                              hotspot_result_t *result);

#ifdef __cplusplus
}
#endif
