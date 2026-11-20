#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t boot_sequence;
    uint64_t uptime_ms;
    uint64_t epoch_ms;
    bool ntp_synced;
} app_time_info_t;

/* NVS stores a monotonically increasing boot sequence. NVS failure only degrades timestamps. */
esp_err_t app_time_init(void);
/* Starts asynchronous SNTP only when WiFi/cloud support is enabled and connected. */
void app_time_start_ntp(void);
void app_time_get(app_time_info_t *info);
uint32_t app_time_boot_sequence(void);
/* UTC ISO-8601 when synchronized, otherwise boot sequence + uptime. */
void app_time_format(char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
