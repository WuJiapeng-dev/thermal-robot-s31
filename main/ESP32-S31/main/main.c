#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_calib.h"
#include "app_cloud.h"
#include "app_config.h"
#include "app_console.h"
#include "app_hotspot.h"
#include "app_link.h"
#include "app_storage.h"
#include "app_thermal.h"
#include "app_time.h"
#include "app_tts.h"
#include "app_vision.h"
#include "bsp_pinmap.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "main"

typedef struct {
    const char *name;
    esp_err_t result;
} init_result_t;

static void print_init_result(const char *name, esp_err_t result)
{
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "INIT %-10s OK", name);
    } else {
        ESP_LOGW(TAG, "INIT %-10s FAIL (%s)", name, esp_err_to_name(result));
    }
}

static void wait_for_thermal(uint32_t timeout_ms)
{
    uint32_t start = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (!app_thermal_connected() &&
           ((uint32_t)(esp_timer_get_time() / 1000ULL) - start) < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void archive_scan(const hotspot_result_t *result, const thermal_frame_t *frame)
{
    app_time_info_t time_info;
    app_time_get(&time_info);
    char json[CLOUD_MAX_JSON_BYTES];
    esp_err_t ret = app_cloud_build_scan_json(json, sizeof(json), result, &time_info,
                                              frame->frame_id, frame->fpa_raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "scan JSON build failed: %s", esp_err_to_name(ret));
        return;
    }

    if (app_storage_mounted()) {
        ret = app_storage_append_scan_json(json);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "structured result append failed: %s", esp_err_to_name(ret));
        }
        ret = app_storage_write_thermal_snapshot(frame->pixels, frame->width, frame->height,
                                                 frame->frame_id, frame->fpa_raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "raw snapshot failed: %s", esp_err_to_name(ret));
        }
        char bmp_path[APP_SNAPSHOT_PATH_LEN];
        ret = app_storage_write_thermal_pseudo_bmp(frame->pixels, frame->width, frame->height,
                                                   frame->frame_id, bmp_path, sizeof(bmp_path));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "pseudo-color snapshot failed: %s", esp_err_to_name(ret));
        } else if (app_cloud_connected()) {
            esp_err_t upload_ret = app_cloud_upload_snapshot(bmp_path);
            if (upload_ret != ESP_OK) {
                ESP_LOGW(TAG, "BMP upload failed: %s", esp_err_to_name(upload_ret));
            }
        }
    }
    if (app_cloud_connected()) {
        ret = app_cloud_upload_hotspot_json(json);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Top-3 scan upload failed: %s", esp_err_to_name(ret));
        }
    }
}

static void run_scan(uint16_t *pixels, uint32_t requested_frames, uint32_t capture_sequence)
{
    app_link_set_stage(APP_STAGE_SCAN, 0);
    app_hotspot_reset(); /* Two confirmations must belong to the same stopped position. */
    hotspot_result_t aggregate;
    memset(&aggregate, 0, sizeof(aggregate));
    thermal_frame_t latest = {
        .pixels = pixels,
        .width = THERMAL_WIDTH,
        .height = THERMAL_HEIGHT,
    };
    uint32_t captured_frames = 0U;

    for (uint32_t i = 0; i < requested_frames; ++i) {
        esp_err_t ret = thermal_get_frame(&latest, THERMAL_FRAME_WAIT_MS);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "scan frame %lu: %s", (unsigned long)(i + 1), esp_err_to_name(ret));
            continue;
        }
        ++captured_frames;
        ret = app_hotspot_analyze(latest.pixels, latest.width, latest.height, &aggregate);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "hotspot analysis: %s", esp_err_to_name(ret));
        }
        app_link_set_stage(APP_STAGE_SCAN, (uint8_t)((i + 1U) * 70U / requested_frames));
    }

    app_link_set_stage(APP_STAGE_ANALYZE, 80);
    ESP_LOGI(TAG, "scan: frames=%lu candidates=%lu confirmed=%u median=%u",
             (unsigned long)captured_frames, (unsigned long)aggregate.candidate_regions,
             aggregate.count, aggregate.median_raw);
    app_storage_logf("scan seq=%lu frames=%lu candidates=%lu confirmed=%u median=%u",
                     (unsigned long)capture_sequence, (unsigned long)captured_frames,
                     (unsigned long)aggregate.candidate_regions, aggregate.count,
                     aggregate.median_raw);

    for (uint8_t i = 0; i < aggregate.count; ++i) {
        const hotspot_t *hotspot = &aggregate.items[i];
        const calib_entry_t *entry = app_calib_find(hotspot->x, hotspot->y);
        const char *group = entry ? entry->group_id : "UNKNOWN";
        const char *zh = entry ? entry->zh_name : "未知区域";
        esp_err_t ret = app_link_send_hotspot(hotspot, group, zh);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "hotspot link send failed: %s", esp_err_to_name(ret));
        }
        bool valid_temp = isfinite(hotspot->temp_c) && hotspot->temp_c > APP_TEMP_INVALID_C + 1.0f;
        ret = app_tts_speak_hotspot(zh, valid_temp, valid_temp ? (int)hotspot->temp_c : 0);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "hotspot TTS queue failed: %s", esp_err_to_name(ret));
        }
        char temp_text[12] = "--";
        if (valid_temp) {
            (void)snprintf(temp_text, sizeof(temp_text), "%.1f", (double)hotspot->temp_c);
        }
        ESP_LOGI(TAG, "hotspot[%u] grp=%s zh=%s x=%u y=%u raw_delta=%lu temp_c=%s conf=%.2f",
                 i, group, zh, hotspot->x, hotspot->y,
                 (unsigned long)hotspot->raw_delta, temp_text, (double)hotspot->conf);
    }

    app_link_send_heartbeat(aggregate.count);
    if (capture_sequence != 0U) {
        /* Acknowledge before slow SD/cloud work so the chassis is not held unnecessarily. */
        esp_err_t ret = app_link_send_capture_done(capture_sequence, captured_frames, aggregate.count);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "capture_done send failed: %s", esp_err_to_name(ret));
        }
    }
    if (captured_frames > 0U) {
        archive_scan(&aggregate, &latest);
    } else {
        ESP_LOGW(TAG, "no fresh thermal frame; snapshot and result archive skipped");
    }
    app_link_set_stage(APP_STAGE_DONE, 100);
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s starting", APP_NAME);
    ESP_LOGI(TAG, "free internal DRAM: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "free PSRAM: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    init_result_t results[] = {
        {.name = "TIME", .result = ESP_OK},
        {.name = "SD", .result = ESP_OK},
        {.name = "CALIB", .result = ESP_OK},
        {.name = "THERMAL", .result = ESP_OK},
        {.name = "VISION", .result = ESP_OK},
        {.name = "AUDIO", .result = ESP_OK},
        {.name = "TTS", .result = ESP_OK},
        {.name = "LINK", .result = ESP_OK},
        {.name = "CLOUD", .result = ESP_OK},
    };

    /* Explicit statements preserve the required initialization order. */
    results[0].result = app_time_init();
    results[1].result = app_storage_init();
    if (results[1].result == ESP_OK) {
        results[2].result = app_calib_init();
    } else {
        results[2].result = ESP_ERR_INVALID_STATE;
    }
    results[3].result = app_thermal_init();
    results[4].result = app_vision_init();
    results[5].result = app_audio_init();
    results[6].result = app_tts_init();
    results[7].result = app_link_init();
    results[8].result = app_cloud_init();

    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        print_init_result(results[i].name, results[i].result);
    }

    esp_err_t hotspot_ret = app_hotspot_init();
    print_init_result("HOTSPOT", hotspot_ret);
    esp_err_t console_ret = app_console_init();
    print_init_result("CONSOLE", console_ret);

    uint16_t *pixels = heap_caps_malloc(THERMAL_PIXEL_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "PSRAM scan buffer allocation failed");
        return;
    }

    wait_for_thermal(10000);
    if (app_thermal_connected()) {
        /* One-time calibration storage; NUC computation is deliberately out of scope. */
        esp_err_t cal_ret = app_thermal_download_calibration();
        ESP_LOGI(TAG, "calibration download: %s", esp_err_to_name(cal_ret));
    }

    while (true) {
        app_capture_request_t request;
        if (app_link_try_get_capture_request(&request) == ESP_OK) {
            run_scan(pixels, request.frames, request.sequence);
        } else if (!app_link_capture_mode_active()) {
            run_scan(pixels, APP_SCAN_FRAMES, 0U);
            vTaskDelay(pdMS_TO_TICKS(APP_SCAN_INTERVAL_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
