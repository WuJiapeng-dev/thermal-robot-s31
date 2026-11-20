#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_tts_init(void);
esp_err_t app_tts_speak(const char *utf8_text);
esp_err_t app_tts_speak_hotspot(const char *zh_name, bool temperature_valid, int temperature_c);
esp_err_t app_tts_self_test(void);

#ifdef __cplusplus
}
#endif
