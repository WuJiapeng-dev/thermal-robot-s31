#include "app_storage.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app_config.h"
#include "app_time.h"
#include "bsp_pinmap.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#define TAG "storage"

static sdmmc_card_t *s_card;
static bool s_mounted;
static SemaphoreHandle_t s_log_mutex;

static esp_err_t make_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    return mkdir(path, 0775) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t app_storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    if (!s_log_mutex) {
        s_log_mutex = xSemaphoreCreateMutex();
        if (!s_log_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = PINMAP_SD_CLK;
    slot.cmd = PINMAP_SD_CMD;
    slot.d0 = PINMAP_SD_D0;
    slot.d1 = PINMAP_SD_D1;
    slot.d2 = PINMAP_SD_D2;
    slot.d3 = PINMAP_SD_D3;
    slot.width = SD_BUS_WIDTH;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_OPEN_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT_BYTES,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted: %llu MB, %s", ((unsigned long long)s_card->csd.capacity) * 512ULL / (1024ULL * 1024ULL),
             s_card->ocr & SD_OCR_SDHC_CAP ? "SDHC/SDXC" : "SD");
    return app_storage_ensure_directories();
}

bool app_storage_mounted(void)
{
    return s_mounted;
}

esp_err_t app_storage_ensure_directories(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *paths[] = {SD_FRAMES_DIR, SD_SNAPSHOTS_DIR, SD_LOGS_DIR};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        esp_err_t ret = make_directory(paths[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "mkdir %s failed: %s", paths[i], esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t app_storage_self_test(void)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "self test: SD not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "self test: opendir failed");
        return ESP_FAIL;
    }
    size_t entries = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ++entries;
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }
    closedir(dir);

    uint8_t write_buffer[1024];
    uint8_t read_buffer[1024];
    memset(write_buffer, 0xA5, sizeof(write_buffer));
    esp_err_t ret = app_storage_write_file(SD_SELF_TEST_PATH, write_buffer, sizeof(write_buffer));
    if (ret == ESP_OK) {
        size_t length = 0;
        ret = app_storage_read_file(SD_SELF_TEST_PATH, read_buffer, sizeof(read_buffer), &length);
    }
    if (ret == ESP_OK && memcmp(write_buffer, read_buffer, sizeof(read_buffer)) != 0) {
        ret = ESP_ERR_INVALID_RESPONSE;
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "self test OK: %u root entries, 1KB write/read verified", (unsigned)entries);
    } else {
        ESP_LOGE(TAG, "self test failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t app_storage_logf(const char *format, ...)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    char line[192];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0) {
        return ESP_FAIL;
    }
    if ((size_t)length >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }

    char timestamp[40];
    app_time_format(timestamp, sizeof(timestamp));
    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    FILE *file = fopen("/sdcard/logs/app.log", "a");
    if (!file) {
        xSemaphoreGive(s_log_mutex);
        return ESP_FAIL;
    }
    int write_ret = fprintf(file, "%s %s\n", timestamp, line);
    int close_ret = fclose(file);
    xSemaphoreGive(s_log_mutex);
    return (write_ret >= 0 && close_ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_storage_append_scan_json(const char *json)
{
    if (!s_mounted || !json || !*json) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    FILE *file = fopen(SD_RESULT_LOG_PATH, "a");
    if (!file) {
        xSemaphoreGive(s_log_mutex);
        return ESP_FAIL;
    }
    int write_ret = fprintf(file, "%s\n", json);
    int close_ret = fclose(file);
    xSemaphoreGive(s_log_mutex);
    return (write_ret >= 0 && close_ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t app_storage_write_thermal_snapshot(const uint16_t *pixels, uint16_t width, uint16_t height,
                                             uint32_t frame_id, uint32_t fpa_raw)
{
    if (!s_mounted || !pixels) {
        return ESP_ERR_INVALID_STATE;
    }
    char path[APP_SNAPSHOT_PATH_LEN];
    snprintf(path, sizeof(path), SD_SNAPSHOTS_DIR "/boot%06lu_frame_%06lu.csv",
             (unsigned long)app_time_boot_sequence(), (unsigned long)frame_id);
    FILE *file = fopen(path, "w");
    if (!file) {
        return ESP_FAIL;
    }
    fprintf(file, "# thermal_robot raw snapshot frame=%lu fpa_raw=%lu width=%u height=%u\n",
            (unsigned long)frame_id, (unsigned long)fpa_raw, width, height);
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            fprintf(file, "%u%s", pixels[(size_t)y * width + x], x + 1 == width ? "\n" : ",");
        }
    }
    int close_ret = fclose(file);
    if (close_ret != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "snapshot written: %s", path);
    return ESP_OK;
}

static void put_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static void pseudo_color(uint8_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (value < 64U) {
        *r = 0U; *g = (uint8_t)(value * 4U); *b = 255U;
    } else if (value < 128U) {
        *r = 0U; *g = 255U; *b = (uint8_t)(255U - (value - 64U) * 4U);
    } else if (value < 192U) {
        *r = (uint8_t)((value - 128U) * 4U); *g = 255U; *b = 0U;
    } else {
        *r = 255U; *g = (uint8_t)(255U - (value - 192U) * 4U); *b = 0U;
    }
}

esp_err_t app_storage_write_thermal_pseudo_bmp(const uint16_t *pixels, uint16_t width, uint16_t height,
                                               uint32_t frame_id, char *path_out, size_t path_capacity)
{
    if (!s_mounted || !pixels || width != THERMAL_WIDTH || height != THERMAL_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[APP_SNAPSHOT_PATH_LEN];
    int path_length = snprintf(path, sizeof(path), SD_SNAPSHOTS_DIR "/boot%06lu_frame_%06lu.bmp",
                               (unsigned long)app_time_boot_sequence(), (unsigned long)frame_id);
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (path_out && (size_t)path_length >= path_capacity) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t min_raw = UINT16_MAX;
    uint16_t max_raw = 0U;
    for (size_t i = 0; i < THERMAL_PIXEL_COUNT; ++i) {
        if (pixels[i] < min_raw) { min_raw = pixels[i]; }
        if (pixels[i] > max_raw) { max_raw = pixels[i]; }
    }
    uint32_t span = (uint32_t)max_raw - min_raw;
    if (span == 0U) { span = 1U; }

    const uint32_t row_bytes = (uint32_t)width * 3U;
    const uint32_t padding = (4U - (row_bytes & 3U)) & 3U;
    const uint32_t stride = row_bytes + padding;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    put_le32(&header[2], 54U + stride * height);
    put_le32(&header[10], 54U);
    put_le32(&header[14], 40U);
    put_le32(&header[18], width);
    put_le32(&header[22], height);
    put_le16(&header[26], 1U);
    put_le16(&header[28], 24U);
    put_le32(&header[34], stride * height);
    put_le32(&header[38], 2835U);
    put_le32(&header[42], 2835U);

    FILE *file = fopen(path, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    esp_err_t ret = fwrite(header, 1, sizeof(header), file) == sizeof(header) ? ESP_OK : ESP_FAIL;
    uint8_t row[THERMAL_WIDTH * 3U + 3U];
    for (uint16_t y = height; ret == ESP_OK && y > 0U; ) {
        --y;
        memset(row, 0, stride);
        for (uint16_t x = 0; x < width; ++x) {
            uint8_t value = (uint8_t)(((uint32_t)pixels[(size_t)y * width + x] - min_raw) * 255U / span);
            uint8_t r, g, b;
            pseudo_color(value, &r, &g, &b);
            row[x * 3U] = b;
            row[x * 3U + 1U] = g;
            row[x * 3U + 2U] = r;
        }
        if (fwrite(row, 1, stride, file) != stride) {
            ret = ESP_FAIL;
        }
    }
    if (fclose(file) != 0) {
        ret = ESP_FAIL;
    }
    if (ret == ESP_OK && path_out) {
        memcpy(path_out, path, (size_t)path_length + 1U);
    }
    ESP_LOGI(TAG, "pseudo-color BMP %s: %s", ret == ESP_OK ? "written" : "failed", path);
    return ret;
}

esp_err_t app_storage_read_file(const char *path, uint8_t *buffer, size_t capacity, size_t *length)
{
    if (!s_mounted || !path || !buffer || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_FAIL;
    }
    size_t total = fread(buffer, 1, capacity, file);
    if (ferror(file)) {
        fclose(file);
        return ESP_FAIL;
    }
    fclose(file);
    *length = total;
    return ESP_OK;
}

esp_err_t app_storage_write_file(const char *path, const uint8_t *buffer, size_t length)
{
    if (!s_mounted || !path || (!buffer && length)) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        return ESP_FAIL;
    }
    size_t written = fwrite(buffer, 1, length, file);
    int close_result = fclose(file);
    return (written == length && close_result == 0) ? ESP_OK : ESP_FAIL;
}
