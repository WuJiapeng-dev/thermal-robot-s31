#include "app_tts.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bsp_pinmap.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "tts"

typedef struct {
    uint32_t unicode;
    uint8_t gb_hi;
    uint8_t gb_lo;
} gb2312_entry_t;

/* Compact conversion subset for the robot's UI and announcement vocabulary.
 * Unknown characters are logged and skipped; ASCII passes through unchanged. */
static const gb2312_entry_t s_gb2312[] = {
{0x4e00, 0xD2, 0xBB}, {0x4e03, 0xC6, 0xDF}, {0x4e09, 0xC8, 0xFD}, {0x4e0a, 0xC9, 0xCF},
{0x4e0b, 0xCF, 0xC2}, {0x4e3b, 0xD6, 0xF7}, {0x4e5d, 0xBE, 0xC9}, {0x4e8c, 0xB6, 0xFE},
{0x4e94, 0xCE, 0xE5}, {0x4ef6, 0xBC, 0xFE}, {0x4f20, 0xB4, 0xAB}, {0x50cf, 0xCF, 0xF1},
{0x5143, 0xD4, 0xAA}, {0x516b, 0xB0, 0xCB}, {0x516d, 0xC1, 0xF9}, {0x5206, 0xB7, 0xD6},
{0x524d, 0xC7, 0xB0}, {0x533a, 0xC7, 0xF8}, {0x5341, 0xCA, 0xAE}, {0x53d1, 0xB7, 0xA2},
{0x53e3, 0xBF, 0xDA}, {0x53f3, 0xD3, 0xD2}, {0x540e, 0xBA, 0xF3}, {0x5668, 0xC6, 0xF7},
{0x56db, 0xCB, 0xC4}, {0x57df, 0xD3, 0xF2}, {0x5934, 0xCD, 0xB7}, {0x5b8c, 0xCD, 0xEA},
{0x5bb9, 0xC8, 0xDD}, {0x5de1, 0xD1, 0xB2}, {0x5de6, 0xD7, 0xF3}, {0x5e38, 0xB3, 0xA3},
{0x5ea6, 0xB6, 0xC8}, {0x5f02, 0xD2, 0xEC}, {0x6001, 0xCC, 0xAC}, {0x611f, 0xB8, 0xD0},
{0x6210, 0xB3, 0xC9}, {0x626b, 0xC9, 0xA8}, {0x62a5, 0xB1, 0xA8}, {0x63a5, 0xBD, 0xD3},
{0x63cf, 0xC3, 0xE8}, {0x6444, 0xC9, 0xE3}, {0x64ad, 0xB2, 0xA5}, {0x6545, 0xB9, 0xCA},
{0x65ad, 0xB6, 0xCF}, {0x673a, 0xBB, 0xFA}, {0x677f, 0xB0, 0xE5}, {0x6790, 0xCE, 0xF6},
{0x67d0, 0xC4, 0xB3}, {0x68c0, 0xBC, 0xEC}, {0x6b63, 0xD5, 0xFD}, {0x6c60, 0xB3, 0xD8},
{0x6d4b, 0xB2, 0xE2}, {0x6e29, 0xCE, 0xC2}, {0x6e90, 0xD4, 0xB4}, {0x70ed, 0xC8, 0xC8},
{0x7247, 0xC6, 0xAC}, {0x72b6, 0xD7, 0xB4}, {0x7535, 0xB5, 0xE7}, {0x82af, 0xD0, 0xBE},
{0x8baf, 0xD1, 0xB6}, {0x8bd5, 0xCA, 0xD4}, {0x8bed, 0xD3, 0xEF}, {0x8fc7, 0xB9, 0xFD},
{0x8fde, 0xC1, 0xAC}, {0x901a, 0xCD, 0xA8}, {0x91cf, 0xC1, 0xBF}, {0x94fe, 0xC1, 0xB4},
{0x963b, 0xD7, 0xE8}, {0x969c, 0xD5, 0xCF}, {0x97f3, 0xD2, 0xF4},
};

static QueueHandle_t s_tts_queue;
static bool s_initialized;

static bool decode_utf8(const uint8_t *text, size_t length, size_t *offset, uint32_t *codepoint)
{
    if (*offset >= length) {
        return false;
    }
    uint8_t first = text[(*offset)++];
    if (first < 0x80) {
        *codepoint = first;
        return true;
    }
    int continuation_count;
    uint32_t value;
    if ((first & 0xE0) == 0xC0) {
        continuation_count = 1;
        value = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        continuation_count = 2;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        continuation_count = 3;
        value = first & 0x07;
    } else {
        return false;
    }
    if (*offset + (size_t)continuation_count > length) {
        return false;
    }
    for (int i = 0; i < continuation_count; ++i) {
        uint8_t next = text[(*offset)++];
        if ((next & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }
    *codepoint = value;
    return true;
}

static bool lookup_gb2312(uint32_t codepoint, uint8_t bytes[2])
{
    for (size_t i = 0; i < sizeof(s_gb2312) / sizeof(s_gb2312[0]); ++i) {
        if (s_gb2312[i].unicode == codepoint) {
            bytes[0] = s_gb2312[i].gb_hi;
            bytes[1] = s_gb2312[i].gb_lo;
            return true;
        }
    }
    return false;
}

static size_t utf8_to_gb2312(const char *utf8_text, uint8_t *gb_buffer, size_t capacity)
{
    const uint8_t *text = (const uint8_t *)utf8_text;
    size_t length = strlen(utf8_text);
    size_t input_offset = 0;
    size_t output_length = 0;
    while (input_offset < length && output_length + 1 < capacity) {
        uint32_t codepoint = 0;
        size_t before = input_offset;
        if (!decode_utf8(text, length, &input_offset, &codepoint)) {
            ESP_LOGW(TAG, "invalid UTF-8 at byte %u", (unsigned)before);
            continue;
        }
        if (codepoint < 0x80) {
            gb_buffer[output_length++] = (uint8_t)codepoint;
            continue;
        }
        uint8_t bytes[2];
        if (lookup_gb2312(codepoint, bytes)) {
            gb_buffer[output_length++] = bytes[0];
            gb_buffer[output_length++] = bytes[1];
        } else {
            ESP_LOGW(TAG, "skip unsupported character U+%04lX for SYN6288 GB2312 table",
                     (unsigned long)codepoint);
        }
    }
    return output_length;
}

static esp_err_t send_gb2312(const uint8_t *gb_text, size_t gb_length)
{
    if (gb_length == 0 || gb_length > TTS_MAX_TEXT_GB2312_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t frame[5];
    frame[0] = 0xFD;
    frame[1] = (uint8_t)(((gb_length + 1U) >> 8) & 0xFFU);
    frame[2] = (uint8_t)((gb_length + 1U) & 0xFFU);
    frame[3] = 0x01;
    frame[4] = 0x00;
    int written = uart_write_bytes(PINMAP_TTS_UART, frame, 4);
    if (written != 4) {
        return ESP_FAIL;
    }
    written = uart_write_bytes(PINMAP_TTS_UART, (const char *)gb_text, (int)gb_length);
    if (written != (int)gb_length) {
        return ESP_FAIL;
    }
    esp_err_t ret = uart_wait_tx_done(PINMAP_TTS_UART, pdMS_TO_TICKS(TTS_UART_TIMEOUT_MS));
    return ret;
}

static void tts_task(void *arg)
{
    (void)arg;
    char text[TTS_MAX_UTF8_BYTES];
    uint8_t *gb_text = malloc(TTS_MAX_GB2312_BYTES);
    if (!gb_text) {
        ESP_LOGE(TAG, "TTS conversion buffer allocation failed");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (xQueueReceive(s_tts_queue, text, portMAX_DELAY) == pdTRUE) {
            size_t gb_length = utf8_to_gb2312(text, gb_text, TTS_MAX_GB2312_BYTES);
            esp_err_t ret = send_gb2312(gb_text, gb_length);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "SYN6288 send failed: %s", esp_err_to_name(ret));
            }
        }
    }
}

static const char *number_to_chinese(int value, char *buffer, size_t capacity)
{
    static const char *const digits[] = {"零", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
    if (value < 0 || value > 99) {
        snprintf(buffer, capacity, "%d", value);
        return buffer;
    }
    if (value < 10) {
        snprintf(buffer, capacity, "%s", digits[value]);
    } else if (value < 20) {
        snprintf(buffer, capacity, "十%s", value == 10 ? "" : digits[value % 10]);
    } else if (value % 10 == 0) {
        snprintf(buffer, capacity, "%s十", digits[value / 10]);
    } else {
        snprintf(buffer, capacity, "%s十%s", digits[value / 10], digits[value % 10]);
    }
    return buffer;
}

esp_err_t app_tts_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    const uart_config_t uart_config = {
        .baud_rate = (int)TTS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_driver_install(PINMAP_TTS_UART, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART1 install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = uart_param_config(PINMAP_TTS_UART, &uart_config);
    if (ret == ESP_OK) {
        ret = uart_set_pin(PINMAP_TTS_UART, PINMAP_TTS_TX, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART1 config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_tts_queue = xQueueCreate(TTS_QUEUE_LENGTH, TTS_MAX_UTF8_BYTES);
    if (!s_tts_queue) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t task_ret = xTaskCreate(tts_task, "tts", TASK_STACK_TTS, NULL,
                                      TASK_PRIORITY_TTS, NULL);
    if (task_ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;
    ESP_LOGW(TAG, "SYN6288 baud %u is pending verification; module must be a 3.3 V logic version",
             (unsigned)TTS_UART_BAUD);
    return ESP_OK;
}

esp_err_t app_tts_speak(const char *utf8_text)
{
    if (!s_initialized || !utf8_text || !*utf8_text) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueSend(s_tts_queue, utf8_text, 0) != pdTRUE) {
        ESP_LOGW(TAG, "announcement queue full; dropped: %s", utf8_text);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t app_tts_speak_hotspot(const char *zh_name, bool temperature_valid, int temperature_c)
{
    char number[32];
    char text[TTS_MAX_UTF8_BYTES];
    if (!zh_name || !*zh_name) {
        zh_name = "某区域";
    }
    if (temperature_valid) {
        snprintf(text, sizeof(text), "%s元件发热，温度%s度", zh_name,
                 number_to_chinese(temperature_c, number, sizeof(number)));
    } else {
        snprintf(text, sizeof(text), "%s元件发热，温度未标定", zh_name);
    }
    return app_tts_speak(text);
}

esp_err_t app_tts_self_test(void)
{
    esp_err_t ret = app_tts_init();
    if (ret != ESP_OK) {
        return ret;
    }
    return app_tts_speak("测试一二三");
}
