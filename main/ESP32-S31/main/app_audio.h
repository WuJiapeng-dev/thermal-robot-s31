#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_audio_init(void);
esp_err_t app_audio_self_test(void);
bool app_audio_initialized(void);

#ifdef __cplusplus
}
#endif
