#include "app_console.h"

#include <stdio.h>
#include <string.h>

#include "app_audio.h"
#include "app_calib.h"
#include "app_config.h"
#include "app_hotspot.h"
#include "app_link.h"
#include "app_storage.h"
#include "app_thermal.h"
#include "app_tts.h"
#include "app_vision.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "console"

static int report_result(esp_err_t ret)
{
    printf("%s: %s\n", ret == ESP_OK ? "OK" : "FAIL", esp_err_to_name(ret));
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_thermal_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_thermal_self_test());
}

static int cmd_usb_probe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_thermal_usb_probe());
}

static int cmd_hotspot_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint16_t *pixels = heap_caps_malloc(THERMAL_PIXEL_BYTES,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        printf("FAIL: PSRAM allocation failed\n");
        return 1;
    }
    for (size_t i = 0; i < THERMAL_PIXEL_COUNT; ++i) {
        pixels[i] = 20000;
    }
    for (uint16_t y = 18; y < 22; ++y) {
        for (uint16_t x = 34; x < 38; ++x) {
            pixels[(size_t)y * THERMAL_WIDTH + x] = 22000;
        }
    }
    esp_err_t ret = app_hotspot_init();
    app_hotspot_reset();
    hotspot_result_t result;
    if (ret == ESP_OK) {
        ret = app_hotspot_analyze(pixels, THERMAL_WIDTH, THERMAL_HEIGHT, &result);
    }
    if (ret == ESP_OK) {
        ret = app_hotspot_analyze(pixels, THERMAL_WIDTH, THERMAL_HEIGHT, &result);
    }
    if (ret == ESP_OK) {
        printf("OK: hotspots=%u median=%u candidates=%lu\n", result.count,
               result.median_raw, (unsigned long)result.candidate_regions);
        for (uint8_t i = 0; i < result.count; ++i) {
            printf("  [%u] x=%u y=%u delta=%lu area=%u conf=%.2f temp=%.1f\n",
                   i, result.items[i].x, result.items[i].y,
                   (unsigned long)result.items[i].raw_delta, result.items[i].area_px,
                   (double)result.items[i].conf, (double)result.items[i].temp_c);
        }
    }
    heap_caps_free(pixels);
    return ret == ESP_OK ? 0 : 1;
}

static int cmd_sd_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_storage_self_test());
}

static int cmd_calib_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_calib_self_test());
}

static int cmd_tts_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_tts_self_test());
}

static int cmd_link_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_link_self_test());
}

static int cmd_cam_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_vision_self_test());
}

static int cmd_audio_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return report_result(app_audio_self_test());
}

static esp_err_t register_command(const char *name, const char *help,
                                  esp_console_cmd_func_t func)
{
    const esp_console_cmd_t command = {
        .command = name,
        .help = help,
        .hint = NULL,
        .func = func,
        .argtable = NULL,
    };
    esp_err_t ret = esp_console_cmd_register(&command);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register %s failed: %s", name, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t app_console_init(void)
{
    const esp_console_config_t console_config = {
        .max_cmdline_length = 128,
        .max_cmdline_args = 8,
    };
    esp_err_t ret = esp_console_init(&console_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret |= register_command("thermal_test", "Collect 10 thermal frames and print rate/size/continuity", cmd_thermal_test);
    ret |= register_command("usb_probe", "Print MAG160Core VID/PID/endpoints/claim state", cmd_usb_probe);
    ret |= register_command("hotspot_test", "Run hotspot logic on fixed synthetic data", cmd_hotspot_test);
    ret |= register_command("sd_test", "Mount/list/read/write SD self-test", cmd_sd_test);
    ret |= register_command("calib_test", "Print calibration table count and first entries", cmd_calib_test);
    ret |= register_command("tts_test", "Speak test phrase", cmd_tts_test);
    ret |= register_command("link_test", "Send 5 heartbeat frames and 1 hotspot frame to STM32", cmd_link_test);
    ret |= register_command("cam_test", "Probe OV2640 SCCB and print PID/VER", cmd_cam_test);
    ret |= register_command("audio_test", "Capture 1 second and print peak", cmd_audio_test);
    if (ret != ESP_OK) {
        return ret;
    }

    const esp_console_repl_config_t repl_config = {
        .max_history_len = 8,
        .history_save_path = NULL,
        .task_stack_size = TASK_STACK_CONSOLE,
        .task_priority = TASK_PRIORITY_CONSOLE,
        .prompt = "s31> ",
    };
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_repl_t *repl = NULL;
    ret = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
    if (ret == ESP_OK) {
        ret = esp_console_start_repl(repl);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "console REPL start failed: %s", esp_err_to_name(ret));
    }
    return ret;
}
