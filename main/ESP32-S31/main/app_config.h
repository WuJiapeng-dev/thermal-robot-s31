#pragma once

#include <stdint.h>

#define APP_NAME                        "thermal_robot_s31"
#define APP_TEMP_INVALID_C              (-273.0f)

/* Relative raw-count criterion. 1500 is an estimate awaiting black-body/field calibration;
 * it is NOT a conversion of 8 degrees Celsius and must not be presented as such. */
#define HOTSPOT_RAW_DELTA               1500U
#define HOTSPOT_MIN_AREA_PX             3U
#define HOTSPOT_MAX_RESULTS             8U
#define HOTSPOT_MATCH_RADIUS_PX         3U
#define HOTSPOT_CONSECUTIVE_FRAMES      2U

#define THERMAL_WIDTH                   160U
#define THERMAL_HEIGHT                  120U
#define THERMAL_PIXEL_COUNT             (THERMAL_WIDTH * THERMAL_HEIGHT)
#define THERMAL_PIXEL_BYTES             (THERMAL_PIXEL_COUNT * 2U)
#define THERMAL_HEADER_BYTES            28U
#define THERMAL_TAIL_BYTES              28U
#define THERMAL_FRAME_BYTES             (THERMAL_HEADER_BYTES + THERMAL_PIXEL_BYTES + THERMAL_TAIL_BYTES)
#define THERMAL_FRAME_MAGIC_HEAD        0x1BB1B11BU
#define THERMAL_FRAME_MAGIC_TAIL        0x1BB1B11CU
#define THERMAL_USB_VID                 0x833CU
#define THERMAL_USB_PID_1               0x0001U
#define THERMAL_USB_PID_2               0x0002U
#define THERMAL_EP_CMD_OUT              0x03U
#define THERMAL_EP_ACK_IN               0x82U
#define THERMAL_EP_IMAGE_IN             0x81U
#define THERMAL_EP_CALIB_IN             0x84U
#define THERMAL_EP_CALIB_OUT            0x05U
#define THERMAL_ACK_MAX_BYTES           4096U
#define THERMAL_USB_CHUNK_BYTES         4096U /* 64 full-speed packets. */
#define THERMAL_USB_TRANSFER_COUNT      2U
#define THERMAL_USB_CMD_TIMEOUT_MS      2000U
#define THERMAL_FRAME_WAIT_MS           2000U
#define THERMAL_RECONNECT_DELAY_MS      1000U
#define THERMAL_RETRY_WAIT_MS           5000U
#define THERMAL_MAX_CONSECUTIVE_FAILS   5U
#define THERMAL_CMD_GET_PARAMETER_1     0x6BB6B66BU
#define THERMAL_CMD_GET_PARAMETER_LAST  0x6BB6B679U
#define THERMAL_CMD_SET_SHUTTER_STATE   0x6BB6B672U
#define THERMAL_CMD_START_IMAGE         0x6BB6B673U
#define THERMAL_CMD_STOP_IMAGE          0x6BB6B674U
#define THERMAL_CALIB_HEADER_BYTES      128U
#define THERMAL_CALIB_MAP_COUNT         48U
#define THERMAL_CALIB_BYTES             (THERMAL_CALIB_HEADER_BYTES + THERMAL_CALIB_MAP_COUNT * THERMAL_PIXEL_BYTES)

#define APP_SCAN_FRAMES                 30U
#define APP_SCAN_INTERVAL_MS            5000U
#define APP_CAPTURE_DEFAULT_FRAMES      2U  /* Stop-and-capture handshake: two fresh frames. */
#define APP_CAPTURE_MAX_FRAMES          30U
#define APP_SNAPSHOT_PATH_LEN           128U
#define APP_TIME_MIN_VALID_EPOCH_S      1704067200L /* 2024-01-01: rejects factory-reset RTC time. */

#define SD_MOUNT_POINT                  "/sdcard"
#define SD_CALIB_PATH                   "/sdcard/calib.csv"
#define SD_FRAMES_DIR                   "/sdcard/frames"
#define SD_SNAPSHOTS_DIR                "/sdcard/snapshots"
#define SD_LOGS_DIR                     "/sdcard/logs"
#define SD_RESULT_LOG_PATH              "/sdcard/logs/scan_results.jsonl"
#define SD_SELF_TEST_PATH               "/sdcard/logs/s31_sd_test.bin"
#define SD_BUS_WIDTH                    4U
#define SD_MAX_FREQ_KHZ                 20000U
#define SD_MAX_OPEN_FILES               5U
#define SD_ALLOC_UNIT_BYTES             (16U * 1024U)
#define CALIB_MAX_ENTRIES               64U
#define CALIB_GROUP_ID_BYTES            24U
#define CALIB_ZH_NAME_BYTES             49U /* Up to 12 Chinese characters in UTF-8 plus NUL. */
#define CALIB_MAX_LINE_BYTES            256U

#define CAM_SCCB_ADDR                   0x30U
#define CAM_REG_PID                     0x0AU
#define CAM_REG_VER                     0x0BU
#define CAM_I2C_FREQ_HZ                 100000U
#define CAM_I2C_TIMEOUT_MS              200U

/* 24 kHz matches the local xiaozhi board definition; electrical/audio validation is still pending. */
#define AUDIO_SAMPLE_RATE_HZ            24000U
#define AUDIO_BITS_PER_SAMPLE           16U
#define AUDIO_I2C_FREQ_HZ               100000U
#define AUDIO_CODEC_ADDR_8BIT           0x30U
#define AUDIO_CODEC_ADDR_7BIT           (AUDIO_CODEC_ADDR_8BIT >> 1)
#define AUDIO_MIC_GAIN_DB               30.0f
#define AUDIO_SELF_TEST_SECONDS         1U
#define AUDIO_READ_CHUNK_FRAMES         256U
#define AUDIO_READ_TIMEOUT_MS           500U
#define AUDIO_DMA_DESC_NUM              6U
#define AUDIO_DMA_FRAME_NUM             240U

#define TTS_UART_BAUD                   9600U /* Pending verification against the actual SYN6288 module. */
#define TTS_QUEUE_LENGTH                4U
#define TTS_MAX_UTF8_BYTES              192U
#define TTS_MAX_GB2312_BYTES            (TTS_MAX_UTF8_BYTES * 2U)
#define TTS_UART_TIMEOUT_MS             2000U
#define TTS_MAX_TEXT_GB2312_BYTES       65533U

#define LINK_UART_BAUD                  921600U
#define LINK_JSON_MAX_BYTES             512U
#define LINK_QUEUE_LENGTH               16U
#define LINK_UART_TIMEOUT_MS            500U
#define LINK_HEARTBEAT_PERIOD_MS        1000U
#define LINK_TEST_TIMEOUT_MS            3000U

/* WiFi/cloud are opt-in. Empty credentials/URL never cause a network connection attempt. */
#define APP_WIFI_ENABLE                 0
#define APP_WIFI_SSID                   ""
#define APP_WIFI_PASSWORD               ""
#define APP_CLOUD_UPLOAD_URL            ""
#define APP_CLOUD_SNAPSHOT_URL          ""
#define APP_NTP_SERVER                  "pool.ntp.org"
#define WIFI_CONNECT_TIMEOUT_MS         12000U
#define WIFI_RETRY_DELAY_MS             5000U
#define CLOUD_HTTP_TIMEOUT_MS           5000U
#define CLOUD_MAX_JSON_BYTES            1024U

#define TASK_STACK_USB_LIB              4096U
#define TASK_STACK_USB_CLIENT           4096U
#define TASK_STACK_THERMAL              8192U
#define TASK_STACK_TTS                  4096U
#define TASK_STACK_LINK                 4096U
#define TASK_STACK_CONSOLE              4096U
#define TASK_PRIORITY_USB_LIB           6U
#define TASK_PRIORITY_USB_CLIENT        6U
#define TASK_PRIORITY_THERMAL           5U
#define TASK_PRIORITY_TTS               3U
#define TASK_PRIORITY_LINK              4U
#define TASK_PRIORITY_CONSOLE           2U
