#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char group_id[CALIB_GROUP_ID_BYTES];
    char zh_name[CALIB_ZH_NAME_BYTES];
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
} calib_entry_t;

esp_err_t app_calib_init(void);
void app_calib_reset(void);
size_t app_calib_count(void);
const calib_entry_t *app_calib_entries(void);
const calib_entry_t *app_calib_find(uint16_t x, uint16_t y);
esp_err_t app_calib_self_test(void);
bool app_calib_available(void);

#ifdef __cplusplus
}
#endif
