#include "app_calib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_storage.h"
#include "esp_log.h"

#define TAG "calib"

static calib_entry_t s_entries[CALIB_MAX_ENTRIES];
static size_t s_count;
static bool s_valid;

static bool parse_u16(char **cursor, uint16_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(*cursor, &end, 10);
    if (end == *cursor || parsed > 65535UL) {
        return false;
    }
    *value = (uint16_t)parsed;
    *cursor = end;
    return true;
}

static bool consume_comma(char **cursor)
{
    if (**cursor != ',') {
        return false;
    }
    ++(*cursor);
    return true;
}

static bool parse_csv_line(char *line, calib_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    char *cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    if (*cursor == '#' || *cursor == '\0' || *cursor == '\r' || *cursor == '\n') {
        return false;
    }

    char *comma = strchr(cursor, ',');
    if (!comma || comma == cursor) {
        return false;
    }
    size_t id_length = (size_t)(comma - cursor);
    if (id_length >= sizeof(entry->group_id)) {
        return false;
    }
    memcpy(entry->group_id, cursor, id_length);
    cursor = comma + 1;

    comma = strchr(cursor, ',');
    if (!comma || comma == cursor) {
        return false;
    }
    size_t zh_length = (size_t)(comma - cursor);
    if (zh_length >= sizeof(entry->zh_name)) {
        return false;
    }
    memcpy(entry->zh_name, cursor, zh_length);
    cursor = comma + 1;

    if (!parse_u16(&cursor, &entry->x0) || !consume_comma(&cursor) ||
        !parse_u16(&cursor, &entry->y0) || !consume_comma(&cursor) ||
        !parse_u16(&cursor, &entry->x1) || !consume_comma(&cursor) ||
        !parse_u16(&cursor, &entry->y1)) {
        return false;
    }
    return true;
}

esp_err_t app_calib_init(void)
{
    app_calib_reset();
    if (!app_storage_mounted()) {
        ESP_LOGE(TAG, "calib.csv not loaded: SD unavailable; coordinates-only mode active");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(SD_CALIB_PATH, "r");
    if (!file) {
        ESP_LOGE(TAG, "open %s failed; coordinates-only mode active", SD_CALIB_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    char line[CALIB_MAX_LINE_BYTES];
    while (fgets(line, sizeof(line), file)) {
        if (s_count >= CALIB_MAX_ENTRIES) {
            ESP_LOGW(TAG, "table truncated at %u entries", (unsigned)CALIB_MAX_ENTRIES);
            break;
        }
        calib_entry_t entry;
        if (parse_csv_line(line, &entry)) {
            if (entry.x0 > entry.x1 || entry.y0 > entry.y1 ||
                entry.x1 >= THERMAL_WIDTH || entry.y1 >= THERMAL_HEIGHT) {
                ESP_LOGE(TAG, "invalid rectangle for %s", entry.group_id);
                fclose(file);
                return ESP_ERR_INVALID_RESPONSE;
            }
            s_entries[s_count++] = entry;
        }
    }
    fclose(file);

    if (s_count == 0) {
        ESP_LOGE(TAG, "no valid entries in %s", SD_CALIB_PATH);
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_valid = true;
    ESP_LOGI(TAG, "loaded %u calibration groups", (unsigned)s_count);
    return ESP_OK;
}

void app_calib_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    s_valid = false;
}

size_t app_calib_count(void)
{
    return s_count;
}

const calib_entry_t *app_calib_entries(void)
{
    return s_entries;
}

const calib_entry_t *app_calib_find(uint16_t x, uint16_t y)
{
    if (!s_valid) {
        return NULL;
    }
    for (size_t i = 0; i < s_count; ++i) {
        const calib_entry_t *entry = &s_entries[i];
        if (x >= entry->x0 && x <= entry->x1 && y >= entry->y0 && y <= entry->y1) {
            return entry;
        }
    }
    return NULL;
}

esp_err_t app_calib_self_test(void)
{
    if (!s_valid) {
        ESP_LOGE(TAG, "calibration table unavailable or invalid");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "entries=%u", (unsigned)s_count);
    for (size_t i = 0; i < s_count && i < 3; ++i) {
        ESP_LOGI(TAG, "  [%u] %s,%s,%u,%u,%u,%u", (unsigned)i,
                 s_entries[i].group_id, s_entries[i].zh_name,
                 s_entries[i].x0, s_entries[i].y0, s_entries[i].x1, s_entries[i].y1);
    }
    return ESP_OK;
}

bool app_calib_available(void)
{
    return s_valid;
}
