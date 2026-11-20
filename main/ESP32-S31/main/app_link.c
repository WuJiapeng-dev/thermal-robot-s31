#include "app_link.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_time.h"
#include "bsp_pinmap.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "link"
#define LINK_RX_LINE_BYTES 128U
#define LINK_RX_CHUNK_BYTES 32U

static QueueHandle_t s_link_queue;
static QueueHandle_t s_capture_queue;
static uint32_t s_sent_bytes;
static volatile bool s_capture_mode;

static const char *stage_name(app_stage_t stage)
{
    switch (stage) {
    case APP_STAGE_SCAN:
        return "scan";
    case APP_STAGE_ANALYZE:
        return "analyze";
    case APP_STAGE_DONE:
        return "done";
    case APP_STAGE_IDLE:
    default:
        return "idle";
    }
}

static size_t utf8_prefix_chars(const char *text, size_t max_chars)
{
    size_t bytes = 0;
    size_t chars = 0;
    while (text[bytes] && chars < max_chars) {
        unsigned char c = (unsigned char)text[bytes];
        size_t width = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        bytes += width;
        ++chars;
    }
    return bytes;
}

static size_t json_escape(const char *input, char *output, size_t capacity)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p && used + 2 < capacity; ++p) {
        if (*p == '"' || *p == '\\') {
            output[used++] = '\\';
            output[used++] = (char)*p;
        } else if (*p >= 0x20) {
            output[used++] = (char)*p;
        }
    }
    output[used] = '\0';
    return used;
}

static esp_err_t enqueue_json(const char *json)
{
    if (!s_link_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(json) + 1 > LINK_JSON_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xQueueSend(s_link_queue, json, 0) != pdTRUE) {
        ESP_LOGW(TAG, "link queue full; frame dropped");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static const char *json_value(const char *json, const char *key)
{
    char pattern[24];
    int length = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (length <= 0 || (size_t)length >= sizeof(pattern)) {
        return NULL;
    }
    const char *p = strstr(json, pattern);
    if (!p) {
        return NULL;
    }
    p += (size_t)length;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p++ != ':') {
        return NULL;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return p;
}

static bool json_string_field(const char *json, const char *key, char *out, size_t capacity)
{
    const char *p = json_value(json, key);
    if (!p || *p++ != '"' || capacity == 0U) {
        return false;
    }
    size_t used = 0;
    while (*p && *p != '"') {
        if (*p == '\\' || (unsigned char)*p < 0x20U || used + 1U >= capacity) {
            return false;
        }
        out[used++] = *p++;
    }
    if (*p != '"') {
        return false;
    }
    out[used] = '\0';
    return true;
}

static bool json_u32_field(const char *json, const char *key, uint32_t *out)
{
    const char *p = json_value(json, key);
    if (!p || *p < '0' || *p > '9') {
        return false;
    }
    uint32_t value = 0U;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    *out = value;
    return true;
}

static void handle_rx_line(const char *line)
{
    char command[16];
    if (!json_string_field(line, "cmd", command, sizeof(command))) {
        ESP_LOGW(TAG, "ignored malformed STM32 command");
        return;
    }
    if (strcmp(command, "capture") != 0) {
        return; /* ready/ack are compatibility messages and need no action here. */
    }

    app_capture_request_t request = {.sequence = 0U, .frames = APP_CAPTURE_DEFAULT_FRAMES};
    if (!json_u32_field(line, "seq", &request.sequence) || request.sequence == 0U) {
        ESP_LOGW(TAG, "capture command rejected: missing/invalid seq");
        return;
    }
    uint32_t requested_frames;
    if (json_u32_field(line, "frames", &requested_frames)) {
        request.frames = requested_frames;
    }
    if (request.frames < HOTSPOT_CONSECUTIVE_FRAMES) {
        request.frames = HOTSPOT_CONSECUTIVE_FRAMES;
    }
    if (request.frames > APP_CAPTURE_MAX_FRAMES) {
        request.frames = APP_CAPTURE_MAX_FRAMES;
    }

    s_capture_mode = true;
    if (!s_capture_queue || xQueueSend(s_capture_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "capture queue full; seq=%lu dropped", (unsigned long)request.sequence);
        return;
    }
    ESP_LOGI(TAG, "capture request seq=%lu frames=%lu",
             (unsigned long)request.sequence, (unsigned long)request.frames);
}

static void poll_uart_rx(void)
{
    static char line[LINK_RX_LINE_BYTES];
    static size_t used;
    static bool discarding;
    uint8_t data[LINK_RX_CHUNK_BYTES];
    int count = uart_read_bytes(PINMAP_LINK_UART, data, sizeof(data), 0);
    if (count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        char ch = (char)data[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (!discarding && used > 0U) {
                line[used] = '\0';
                handle_rx_line(line);
            } else if (discarding) {
                ESP_LOGW(TAG, "oversized STM32 command dropped");
            }
            used = 0U;
            discarding = false;
        } else if (!discarding) {
            if (used + 1U < sizeof(line)) {
                line[used++] = ch;
            } else {
                used = 0U;
                discarding = true;
            }
        }
    }
}

static void link_task(void *arg)
{
    (void)arg;
    char json[LINK_JSON_MAX_BYTES];
    uint64_t last_heartbeat = 0;
    for (;;) {
        poll_uart_rx();
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (now - last_heartbeat >= LINK_HEARTBEAT_PERIOD_MS) {
            app_link_send_heartbeat(0);
            last_heartbeat = now;
        }
        if (xQueueReceive(s_link_queue, json, pdMS_TO_TICKS(100)) == pdTRUE) {
            int written = uart_write_bytes(PINMAP_LINK_UART, json, strlen(json));
            if (written > 0) {
                s_sent_bytes += (uint32_t)written;
            }
            uart_write_bytes(PINMAP_LINK_UART, "\n", 1);
            ++s_sent_bytes;
            esp_err_t ret = uart_wait_tx_done(PINMAP_LINK_UART,
                                              pdMS_TO_TICKS(LINK_UART_TIMEOUT_MS));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "USART2 TX timeout");
            }
        }
    }
}

esp_err_t app_link_init(void)
{
    if (s_link_queue) {
        return ESP_OK;
    }
    const uart_config_t uart_config = {
        .baud_rate = (int)LINK_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_driver_install(PINMAP_LINK_UART, 2048, 2048, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USART2 driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = uart_param_config(PINMAP_LINK_UART, &uart_config);
    if (ret == ESP_OK) {
        ret = uart_set_pin(PINMAP_LINK_UART, PINMAP_LINK_UART_TX, PINMAP_LINK_UART_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USART2 config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_link_queue = xQueueCreate(LINK_QUEUE_LENGTH, LINK_JSON_MAX_BYTES);
    s_capture_queue = xQueueCreate(4U, sizeof(app_capture_request_t));
    if (!s_link_queue || !s_capture_queue) {
        return ESP_ERR_NO_MEM;
    }
    s_capture_mode = false;
    BaseType_t task_ret = xTaskCreate(link_task, "stm32_link", TASK_STACK_LINK, NULL,
                                      TASK_PRIORITY_LINK, NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "STM32 link initialized: %u baud, capture RX enabled", (unsigned)LINK_UART_BAUD);
    return ESP_OK;
}

static double valid_or_sentinel(float value)
{
    return (isfinite(value) && value > APP_TEMP_INVALID_C + 1.0f)
               ? (double)value : (double)APP_TEMP_INVALID_C;
}

esp_err_t app_link_send_hotspot(const hotspot_t *hotspot, const char *group_id, const char *zh_name)
{
    if (!hotspot) {
        return ESP_ERR_INVALID_ARG;
    }
    char group_raw[32] = {0};
    char zh_raw[CALIB_ZH_NAME_BYTES] = {0};
    char group_json[72] = {0};
    char zh_json[128] = {0};
    size_t group_bytes = utf8_prefix_chars(group_id ? group_id : "UNKNOWN", 31);
    size_t zh_bytes = utf8_prefix_chars(zh_name && *zh_name ? zh_name : "未知区域", 12);
    memcpy(group_raw, group_id ? group_id : "UNKNOWN", group_bytes);
    memcpy(zh_raw, zh_name && *zh_name ? zh_name : "未知区域", zh_bytes);
    json_escape(group_raw, group_json, sizeof(group_json));
    json_escape(zh_raw, zh_json, sizeof(zh_json));

    char json[LINK_JSON_MAX_BYTES];
    int length = snprintf(json, sizeof(json),
                          "{\"type\":\"hot\",\"id\":%lu,\"grp\":\"%s\",\"zh\":\"%s\","
                          "\"x\":%u,\"y\":%u,\"t\":%.1f,\"dt\":%.1f,\"px\":%u,\"conf\":%.2f}",
                          (unsigned long)hotspot->raw_delta, group_json, zh_json,
                          hotspot->x, hotspot->y, valid_or_sentinel(hotspot->temp_c),
                          valid_or_sentinel(hotspot->delta_c), hotspot->area_px,
                          (double)hotspot->conf);
    if (length <= 0 || (size_t)length >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return enqueue_json(json);
}

esp_err_t app_link_send_heartbeat(uint32_t hotspot_count)
{
    app_time_info_t time_info;
    app_time_get(&time_info);
    char json[LINK_JSON_MAX_BYTES];
    int length;
    if (time_info.ntp_synced) {
        length = snprintf(json, sizeof(json),
                          "{\"type\":\"hb\",\"ts\":%llu,\"epoch\":%llu,\"boot\":%lu,\"n\":%lu}",
                          (unsigned long long)time_info.uptime_ms,
                          (unsigned long long)(time_info.epoch_ms / 1000ULL),
                          (unsigned long)time_info.boot_sequence,
                          (unsigned long)hotspot_count);
    } else {
        length = snprintf(json, sizeof(json),
                          "{\"type\":\"hb\",\"ts\":%llu,\"boot\":%lu,\"n\":%lu}",
                          (unsigned long long)time_info.uptime_ms,
                          (unsigned long)time_info.boot_sequence,
                          (unsigned long)hotspot_count);
    }
    if (length <= 0 || (size_t)length >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return enqueue_json(json);
}

esp_err_t app_link_set_stage(app_stage_t stage, uint8_t progress)
{
    char json[LINK_JSON_MAX_BYTES];
    int length = snprintf(json, sizeof(json),
                          "{\"type\":\"st\",\"stage\":\"%s\",\"prog\":%u}",
                          stage_name(stage), progress);
    if (length <= 0 || (size_t)length >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return enqueue_json(json);
}

esp_err_t app_link_send_capture_done(uint32_t sequence, uint32_t captured_frames,
                                     uint32_t hotspot_count)
{
    char json[LINK_JSON_MAX_BYTES];
    int length = snprintf(json, sizeof(json),
                          "{\"type\":\"capture_done\",\"seq\":%lu,\"frames\":%lu,\"n\":%lu}",
                          (unsigned long)sequence, (unsigned long)captured_frames,
                          (unsigned long)hotspot_count);
    if (length <= 0 || (size_t)length >= sizeof(json)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return enqueue_json(json);
}

esp_err_t app_link_try_get_capture_request(app_capture_request_t *request)
{
    if (!s_capture_queue || !request) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueReceive(s_capture_queue, request, 0) == pdTRUE ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool app_link_capture_mode_active(void)
{
    return s_capture_mode;
}

esp_err_t app_link_self_test(void)
{
    if (!s_link_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t before = s_sent_bytes;
    for (int i = 0; i < 5; ++i) {
        app_link_send_heartbeat((uint32_t)i);
    }
    hotspot_t hotspot = {
        .x = 34,
        .y = 18,
        .temp_c = APP_TEMP_INVALID_C,
        .delta_c = APP_TEMP_INVALID_C,
        .raw_delta = 1500,
        .area_px = 7,
        .conf = 0.86f,
    };
    esp_err_t ret = app_link_send_hotspot(&hotspot, "U3-PWR", "电源区");
    if (ret != ESP_OK) {
        return ret;
    }
    uint32_t start = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (s_sent_bytes == before &&
           ((uint32_t)(esp_timer_get_time() / 1000ULL) - start) < LINK_TEST_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "link self test: bytes=%u", (unsigned)(s_sent_bytes - before));
    return s_sent_bytes > before ? ESP_OK : ESP_ERR_TIMEOUT;
}

uint32_t app_link_sent_bytes(void)
{
    return s_sent_bytes;
}
