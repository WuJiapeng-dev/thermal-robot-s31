idf_component_register(
    SRCS
        "main.c"
        "app_thermal.c"
        "app_hotspot.c"
        "app_calib.c"
        "app_storage.c"
        "app_time.c"
        "app_vision.c"
        "app_audio.c"
        "app_tts.c"
        "app_link.c"
        "app_cloud.c"
        "app_console.c"
    INCLUDE_DIRS "."
    REQUIRES
        console
        esp_driver_gpio
        esp_driver_uart
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_sdmmc
        esp_event
        esp_http_client
        esp_netif
        esp_psram
        esp_timer
        esp_wifi
        fatfs
        lwip
        nvs_flash
        vfs
        espressif__usb
        espressif__esp_codec_dev
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
