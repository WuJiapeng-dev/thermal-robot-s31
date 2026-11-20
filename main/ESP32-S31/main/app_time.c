#include "app_time.h"

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG "time"

static uint32_t s_boot_sequence = 1U;
#if APP_WIFI_ENABLE
static bool s_ntp_started;
#endif

esp_err_t app_time_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed; boot sequence is session-local: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_handle_t handle;
    ret = nvs_open("app_time", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed; boot sequence is session-local: %s", esp_err_to_name(ret));
        return ret;
    }

    uint32_t previous = 0U;
    esp_err_t get_ret = nvs_get_u32(handle, "boot_seq", &previous);
    if (get_ret != ESP_OK && get_ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return get_ret;
    }
    s_boot_sequence = previous + 1U;
    if (s_boot_sequence == 0U) {
        s_boot_sequence = 1U;
    }
    ret = nvs_set_u32(handle, "boot_seq", s_boot_sequence);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "boot sequence commit failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "boot sequence=%lu", (unsigned long)s_boot_sequence);
    return ESP_OK;
}

void app_time_start_ntp(void)
{
#if APP_WIFI_ENABLE
    if (s_ntp_started) {
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, APP_NTP_SERVER);
    esp_sntp_init();
    s_ntp_started = true;
    ESP_LOGI(TAG, "SNTP started asynchronously via %s", APP_NTP_SERVER);
#else
    ESP_LOGI(TAG, "SNTP disabled; using boot sequence + uptime timestamps");
#endif
}

void app_time_get(app_time_info_t *info)
{
    if (!info) {
        return;
    }
    struct timeval now = {0};
    gettimeofday(&now, NULL);
    info->boot_sequence = s_boot_sequence;
    info->uptime_ms = (uint64_t)esp_timer_get_time() / 1000ULL;
    info->epoch_ms = now.tv_sec > 0 ? ((uint64_t)now.tv_sec * 1000ULL +
                                      (uint64_t)now.tv_usec / 1000ULL) : 0ULL;
    info->ntp_synced = now.tv_sec >= APP_TIME_MIN_VALID_EPOCH_S;
    if (!info->ntp_synced) {
        info->epoch_ms = 0ULL;
    }
}

uint32_t app_time_boot_sequence(void)
{
    return s_boot_sequence;
}

void app_time_format(char *out, size_t capacity)
{
    if (!out || capacity == 0U) {
        return;
    }
    app_time_info_t info;
    app_time_get(&info);
    if (info.ntp_synced) {
        time_t seconds = (time_t)(info.epoch_ms / 1000ULL);
        struct tm utc;
        if (gmtime_r(&seconds, &utc)) {
            (void)snprintf(out, capacity, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                           utc.tm_hour, utc.tm_min, utc.tm_sec);
            return;
        }
    }
    (void)snprintf(out, capacity, "boot%06lu+%llums",
                   (unsigned long)info.boot_sequence,
                   (unsigned long long)info.uptime_ms);
}
