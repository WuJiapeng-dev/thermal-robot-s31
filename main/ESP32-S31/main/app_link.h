#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "app_hotspot.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STAGE_IDLE = 0,
    APP_STAGE_SCAN,
    APP_STAGE_ANALYZE,
    APP_STAGE_DONE,
} app_stage_t;

typedef struct {
    uint32_t sequence;
    uint32_t frames;
} app_capture_request_t;

esp_err_t app_link_init(void);
esp_err_t app_link_send_hotspot(const hotspot_t *hotspot, const char *group_id, const char *zh_name);
esp_err_t app_link_send_heartbeat(uint32_t hotspot_count);
esp_err_t app_link_set_stage(app_stage_t stage, uint8_t progress);
esp_err_t app_link_send_capture_done(uint32_t sequence, uint32_t captured_frames,
                                     uint32_t hotspot_count);
/* Non-blocking. Once the first capture command arrives, periodic autonomous scanning stops. */
esp_err_t app_link_try_get_capture_request(app_capture_request_t *request);
bool app_link_capture_mode_active(void);
esp_err_t app_link_self_test(void);
uint32_t app_link_sent_bytes(void);

#ifdef __cplusplus
}
#endif
