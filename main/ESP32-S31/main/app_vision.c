#include "app_vision.h"

#include "app_config.h"
#include "bsp_pinmap.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define TAG "vision"

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_sccb_dev;
static bool s_initialized;
static bool s_sccb_ack;
static uint8_t s_pid;
static uint8_t s_ver;

static esp_err_t start_xclk(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PINMAP_CAM_XCLK_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ledc_channel_config_t channel = {
        .gpio_num = PINMAP_CAM_XCLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 1,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_sccb_dev, &reg, 1, value, 1,
                                       pdMS_TO_TICKS(CAM_I2C_TIMEOUT_MS));
}

esp_err_t app_vision_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = start_xclk();
    if (ret != ESP_OK) {
        return ret;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = PINMAP_CAM_I2C_PORT,
        .sda_io_num = PINMAP_CAM_SIOD,
        .scl_io_num = PINMAP_CAM_SIOC,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = true},
    };
    ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CAM_SCCB_ADDR,
        .scl_speed_hz = CAM_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_sccb_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera SCCB device add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t app_vision_probe(camera_probe_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    if (!s_initialized) {
        ESP_LOGE(TAG, "SCCB not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = i2c_master_probe(s_i2c_bus, CAM_SCCB_ADDR,
                                     pdMS_TO_TICKS(CAM_I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        s_sccb_ack = false;
        ESP_LOGE(TAG, "SCCB probe 0x%02X failed: %s", CAM_SCCB_ADDR, esp_err_to_name(ret));
        return ret;
    }
    s_sccb_ack = true;
    ret = read_register(CAM_REG_PID, &result->pid);
    if (ret == ESP_OK) {
        ret = read_register(CAM_REG_VER, &result->ver);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PID/VER read failed: %s", esp_err_to_name(ret));
        return ret;
    }
    s_pid = result->pid;
    s_ver = result->ver;
    ESP_LOGI(TAG, "OV2640 SCCB OK: PID=0x%02X VER=0x%02X", result->pid, result->ver);
    return ESP_OK;
}

esp_err_t app_vision_self_test(void)
{
    if (!s_initialized) {
        esp_err_t ret = app_vision_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    camera_probe_result_t result;
    esp_err_t ret = app_vision_probe(&result);
    app_vision_report_status();
    return ret;
}

void app_vision_report_status(void)
{
    if (!s_sccb_ack) {
        ESP_LOGI(TAG, "status: SCCB=no, PID=--, VER=--");
    } else {
        ESP_LOGI(TAG, "status: SCCB=yes, PID=0x%02X, VER=0x%02X", s_pid, s_ver);
    }
}
