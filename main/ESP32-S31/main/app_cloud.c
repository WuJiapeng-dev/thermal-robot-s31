#include "app_cloud.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_calib.h"
#include "app_config.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define TAG "cloud"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1

#if APP_WIFI_ENABLE
static EventGroupHandle_t s_wifi_events;
static bool s_initialized;
static bool s_connected;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}
#endif

esp_err_t app_cloud_init(void)
{
#if !APP_WIFI_ENABLE
    ESP_LOGW(TAG, "WiFi/cloud disabled in app_config.h; local positioning and TTS remain available");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_initialized) {
        return s_connected ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (APP_WIFI_SSID[0] == '\0' || APP_CLOUD_UPLOAD_URL[0] == '\0') {
        ESP_LOGW(TAG, "WiFi credentials or upload URL empty; cloud path disabled");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
        return ESP_ERR_NO_MEM;
    }
    ret = esp_netif_init();
    if (ret == ESP_OK) {
        ret = esp_event_loop_create_default();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "network event init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = ESP_OK;
    esp_netif_create_default_wifi_sta();
    const wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&wifi_init);
    if (ret == ESP_OK) {
        ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &wifi_event_handler, NULL, NULL);
    }
    if (ret == ESP_OK) {
        ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &wifi_event_handler, NULL, NULL);
    }
    const wifi_config_t wifi_config = {
        .sta = {
            .ssid = APP_WIFI_SSID,
            .password = APP_WIFI_PASSWORD,
            .threshold = {.authmode = WIFI_AUTH_WPA2_PSK},
        },
    };
    if (ret == ESP_OK) {
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (ret == ESP_OK) {
        ret = esp_wifi_start();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    s_initialized = true;
    if (bits & WIFI_CONNECTED_BIT) {
        app_time_start_ntp();
        ESP_LOGI(TAG, "WiFi connected; hotspot upload enabled");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "WiFi connect timeout; local functions remain available");
    return ESP_ERR_TIMEOUT;
#endif
}

bool app_cloud_connected(void)
{
#if APP_WIFI_ENABLE
    return s_connected;
#else
    return false;
#endif
}

static esp_err_t append_format(char *out, size_t capacity, size_t *used, const char *format, ...)
{
    if (*used >= capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    va_list args;
    va_start(args, format);
    int length = vsnprintf(out + *used, capacity - *used, format, args);
    va_end(args);
    if (length < 0) {
        return ESP_FAIL;
    }
    if ((size_t)length >= capacity - *used) {
        return ESP_ERR_INVALID_SIZE;
    }
    *used += (size_t)length;
    return ESP_OK;
}

static size_t escape_json(const char *input, char *output, size_t capacity)
{
    size_t used = 0U;
    for (const unsigned char *p = (const unsigned char *)input; *p && used + 2U < capacity; ++p) {
        if (*p == '"' || *p == '\\') {
            output[used++] = '\\';
            output[used++] = (char)*p;
        } else if (*p >= 0x20U) {
            output[used++] = (char)*p;
        }
    }
    output[used] = '\0';
    return used;
}

static double valid_or_sentinel(float value)
{
    return (isfinite(value) && value > APP_TEMP_INVALID_C + 1.0f)
               ? (double)value : (double)APP_TEMP_INVALID_C;
}

esp_err_t app_cloud_build_scan_json(char *out, size_t capacity, const hotspot_result_t *result,
                                    const app_time_info_t *time_info, uint32_t frame_id,
                                    uint32_t fpa_raw)
{
    if (!out || capacity == 0U || !result || !time_info) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t used = 0U;
    esp_err_t ret = append_format(out, capacity, &used,
                                  "{\"type\":\"scan\",\"clock\":\"%s\",\"boot\":%lu,"
                                  "\"uptime_ms\":%llu,",
                                  time_info->ntp_synced ? "ntp" : "boot",
                                  (unsigned long)time_info->boot_sequence,
                                  (unsigned long long)time_info->uptime_ms);
    if (ret == ESP_OK && time_info->ntp_synced) {
        ret = append_format(out, capacity, &used, "\"epoch_ms\":%llu,",
                            (unsigned long long)time_info->epoch_ms);
    } else if (ret == ESP_OK) {
        ret = append_format(out, capacity, &used, "\"epoch_ms\":null,");
    }
    if (ret == ESP_OK) {
        ret = append_format(out, capacity, &used,
                            "\"frame_id\":%lu,\"fpa_raw\":%lu,\"median_raw\":%u,\"hotspots\":[",
                            (unsigned long)frame_id, (unsigned long)fpa_raw, result->median_raw);
    }

    uint8_t count = result->count < 3U ? result->count : 3U;
    for (uint8_t i = 0; ret == ESP_OK && i < count; ++i) {
        const hotspot_t *hotspot = &result->items[i];
        const calib_entry_t *entry = app_calib_find(hotspot->x, hotspot->y);
        char group[72];
        char zh[128];
        escape_json(entry ? entry->group_id : "UNKNOWN", group, sizeof(group));
        escape_json(entry ? entry->zh_name : "未知区域", zh, sizeof(zh));
        ret = append_format(out, capacity, &used,
                            "%s{\"id\":%lu,\"grp\":\"%s\",\"zh\":\"%s\",\"x\":%u,\"y\":%u,"
                            "\"t\":%.1f,\"dt\":%.1f,\"px\":%u,\"conf\":%.2f}",
                            i == 0U ? "" : ",", (unsigned long)hotspot->raw_delta, group, zh,
                            hotspot->x, hotspot->y, valid_or_sentinel(hotspot->temp_c),
                            valid_or_sentinel(hotspot->delta_c), hotspot->area_px,
                            (double)hotspot->conf);
    }
    if (ret == ESP_OK) {
        ret = append_format(out, capacity, &used, "]}");
    }
    return ret;
}

static esp_err_t post_buffer(const char *url, const char *content_type,
                             const void *data, size_t length)
{
#if !APP_WIFI_ENABLE
    (void)url;
    (void)content_type;
    (void)data;
    (void)length;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_connected || !url || !*url) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = CLOUD_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = esp_http_client_set_method(client, HTTP_METHOD_POST);
    if (ret == ESP_OK) {
        ret = esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_set_post_field(client, (const char *)data, (int)length);
    }
    if (ret == ESP_OK) {
        ret = esp_http_client_perform(client);
    }
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ret = ESP_ERR_INVALID_RESPONSE;
        }
    }
    esp_http_client_cleanup(client);
    return ret;
#endif
}

esp_err_t app_cloud_upload_hotspot_json(const char *json)
{
    if (!json || strlen(json) > CLOUD_MAX_JSON_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    return post_buffer(APP_CLOUD_UPLOAD_URL, "application/json", json, strlen(json));
}

esp_err_t app_cloud_upload_snapshot(const char *snapshot_path)
{
#if !APP_WIFI_ENABLE
    (void)snapshot_path;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!snapshot_path || APP_CLOUD_SNAPSHOT_URL[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(snapshot_path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length <= 0 || length > 256 * 1024) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *buffer = malloc((size_t)length);
    if (!buffer) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    size_t read_length = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_length != (size_t)length) {
        free(buffer);
        return ESP_FAIL;
    }
    const char *extension = strrchr(snapshot_path, '.');
    const char *content_type = extension && strcmp(extension, ".bmp") == 0 ? "image/bmp" : "text/csv";
    esp_err_t ret = post_buffer(APP_CLOUD_SNAPSHOT_URL, content_type, buffer, read_length);
    free(buffer);
    return ret;
#endif
}
