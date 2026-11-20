#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_hotspot.h"
#include "app_time.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_cloud_init(void);
bool app_cloud_connected(void);
/* Builds one structured scan record containing up to three confirmed hotspots. */
esp_err_t app_cloud_build_scan_json(char *out, size_t capacity, const hotspot_result_t *result,
                                    const app_time_info_t *time_info, uint32_t frame_id,
                                    uint32_t fpa_raw);
esp_err_t app_cloud_upload_hotspot_json(const char *json);
esp_err_t app_cloud_upload_snapshot(const char *snapshot_path);

#ifdef __cplusplus
}
#endif
