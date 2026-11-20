#include "app_audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bsp_pinmap.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <audio_codec_ctrl_if.h>
#include <audio_codec_data_if.h>
#include <audio_codec_gpio_if.h>
#include <audio_codec_if.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <es8311_codec.h>

#define TAG "audio"

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_rx_channel;
static i2s_chan_handle_t s_tx_channel;
static const audio_codec_data_if_t *s_data_if;
static const audio_codec_ctrl_if_t *s_ctrl_if;
static const audio_codec_gpio_if_t *s_gpio_if;
static const audio_codec_if_t *s_codec_if;
static esp_codec_dev_handle_t s_codec_dev;
static bool s_initialized;

static esp_err_t init_i2c(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = PINMAP_AUDIO_I2C_PORT,
        .sda_io_num = PINMAP_AUDIO_I2C_SDA,
        .scl_io_num = PINMAP_AUDIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true},
    };
    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio I2C init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t init_i2s(void)
{
    const i2s_chan_config_t channel_config = {
        .id = PINMAP_AUDIO_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t ret = i2s_new_channel(&channel_config, &s_tx_channel, &s_rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const i2s_std_config_t std_config = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = PINMAP_AUDIO_I2S_MCLK,
            .bclk = PINMAP_AUDIO_I2S_BCLK,
            .ws = PINMAP_AUDIO_I2S_WS,
            .dout = PINMAP_AUDIO_I2S_DOUT,
            .din = PINMAP_AUDIO_I2S_DIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
        },
    };
    ret = i2s_channel_init_std_mode(s_tx_channel, &std_config);
    if (ret == ESP_OK) {
        ret = i2s_channel_init_std_mode(s_rx_channel, &std_config);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_tx_channel);
    }
    if (ret == ESP_OK) {
        ret = i2s_channel_enable(s_rx_channel);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t reset_codec(void)
{
    uint8_t reset_value = 0x1F;
    esp_err_t ret = s_ctrl_if->write_reg(s_ctrl_if, 0x00, 1, &reset_value, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 reset write failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

esp_err_t app_audio_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = init_i2c();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = init_i2s();
    if (ret != ESP_OK) {
        return ret;
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = PINMAP_AUDIO_I2S_PORT,
        .rx_handle = s_rx_channel,
        .tx_handle = s_tx_channel,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_data_if) {
        ESP_LOGE(TAG, "audio data interface create failed");
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = PINMAP_AUDIO_I2C_PORT,
        .addr = AUDIO_CODEC_ADDR_7BIT,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_ctrl_if) {
        ESP_LOGE(TAG, "audio control interface create failed");
        return ESP_ERR_NO_MEM;
    }
    ret = reset_codec();
    if (ret != ESP_OK) {
        return ret;
    }

    s_gpio_if = audio_codec_new_gpio();
    if (!s_gpio_if) {
        ESP_LOGE(TAG, "audio GPIO interface create failed");
        return ESP_ERR_NO_MEM;
    }
    gpio_config_t pa_config = {
        .pin_bit_mask = 1ULL << PINMAP_AUDIO_PA,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&pa_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PA GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(PINMAP_AUDIO_PA, 0);

    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = PINMAP_AUDIO_PA,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
        .pa_reverted = false,
    };
    s_codec_if = es8311_codec_new(&codec_cfg);
    if (!s_codec_if) {
        ESP_LOGE(TAG, "ES8311 codec create failed");
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t device_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec_dev = esp_codec_dev_new(&device_cfg);
    if (!s_codec_dev) {
        ESP_LOGE(TAG, "codec device create failed");
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };
    ret = esp_codec_dev_open(s_codec_dev, &sample_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "codec open failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_codec_dev_set_in_gain(s_codec_dev, AUDIO_MIC_GAIN_DB);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "codec input gain failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "board ES8311 initialized at %u Hz", (unsigned)AUDIO_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t app_audio_self_test(void)
{
    if (!s_initialized) {
        esp_err_t ret = app_audio_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

#if AUDIO_USE_EXTERNAL_I2S_MIC
    const size_t frames = AUDIO_SAMPLE_RATE_HZ * AUDIO_SELF_TEST_SECONDS;
    int16_t *samples = malloc(frames * sizeof(int16_t));
    if (!samples) {
        return ESP_ERR_NO_MEM;
    }
    size_t read_bytes = 0;
    esp_err_t ret = i2s_channel_read(s_rx_channel, samples, frames * sizeof(int16_t),
                                     &read_bytes, pdMS_TO_TICKS(AUDIO_READ_TIMEOUT_MS + AUDIO_SELF_TEST_SECONDS * 1000U));
#else
    const size_t frames = AUDIO_SAMPLE_RATE_HZ * AUDIO_SELF_TEST_SECONDS;
    int16_t *samples = malloc(frames * sizeof(int16_t));
    if (!samples) {
        return ESP_ERR_NO_MEM;
    }
    int ret = esp_codec_dev_read(s_codec_dev, samples, frames * sizeof(int16_t));
#endif
    if (ret < 0) {
        ESP_LOGE(TAG, "audio capture failed: %d", ret);
        free(samples);
        return ESP_FAIL;
    }

#if AUDIO_USE_EXTERNAL_I2S_MIC
    size_t valid_frames = read_bytes / sizeof(int16_t);
#else
    size_t valid_frames = frames;
#endif
    int32_t peak = 0;
    double sum_squares = 0.0;
    for (size_t i = 0; i < valid_frames; ++i) {
        int32_t value = samples[i];
        int32_t magnitude = value < 0 ? -value : value;
        if (magnitude > peak) {
            peak = magnitude;
        }
        sum_squares += (double)value * (double)value;
    }
    double rms = valid_frames ? sqrt(sum_squares / (double)valid_frames) : 0.0;
    uint8_t effective_bits = 0;
    while (((uint32_t)peak >> effective_bits) != 0 && effective_bits < 16) {
        ++effective_bits;
    }
    ESP_LOGI(TAG, "audio self test: frames=%u peak=%ld rms=%.1f effective_bits=%u",
             (unsigned)valid_frames, (long)peak, rms, effective_bits);
    free(samples);
    return valid_frames > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

bool app_audio_initialized(void)
{
    return s_initialized;
}
