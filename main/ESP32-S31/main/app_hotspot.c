#include "app_hotspot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_thermal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define TAG "hotspot"

typedef struct {
    uint32_t x_sum;
    uint32_t y_sum;
    uint32_t area;
    uint32_t peak_delta;
    uint16_t x;
    uint16_t y;
} component_t;

static uint16_t *s_sort_work;
static uint8_t *s_visited;
static uint16_t *s_queue;
static component_t s_previous[HOTSPOT_MAX_RESULTS];
static size_t s_previous_count;
static bool s_ready;

static inline void swap_u16(uint16_t *a, uint16_t *b)
{
    uint16_t value = *a;
    *a = *b;
    *b = value;
}

static uint16_t select_kth(uint16_t *values, size_t left, size_t right, size_t kth)
{
    while (left < right) {
        uint16_t pivot = values[left + (right - left) / 2];
        size_t i = left;
        size_t j = right;
        while (i <= j) {
            while (values[i] < pivot) {
                ++i;
            }
            while (values[j] > pivot) {
                if (j == 0) {
                    break;
                }
                --j;
            }
            if (i <= j) {
                swap_u16(&values[i], &values[j]);
                ++i;
                if (j == 0) {
                    break;
                }
                --j;
            }
        }
        if (kth <= j) {
            right = j;
        } else if (kth >= i) {
            left = i;
        } else {
            return values[kth];
        }
    }
    return values[kth];
}

static uint16_t image_median(const uint16_t *pixels, size_t count)
{
    memcpy(s_sort_work, pixels, count * sizeof(uint16_t));
    size_t lower_index = (count - 1) / 2;
    uint16_t lower = select_kth(s_sort_work, 0, count - 1, lower_index);
    uint16_t upper = lower;
    if ((count % 2) == 0) {
        upper = 0xFFFFU;
        for (size_t i = lower_index + 1; i < count; ++i) {
            if (s_sort_work[i] < upper) {
                upper = s_sort_work[i];
            }
        }
    }
    return (uint16_t)(((uint32_t)lower + upper) / 2U);
}

static void insert_component(component_t *list, size_t *count, const component_t *candidate)
{
    size_t position = 0;
    while (position < *count && list[position].peak_delta >= candidate->peak_delta) {
        ++position;
    }
    if (position >= HOTSPOT_MAX_RESULTS) {
        return;
    }
    size_t new_count = (*count < HOTSPOT_MAX_RESULTS) ? *count + 1 : HOTSPOT_MAX_RESULTS;
    for (size_t i = new_count - 1; i > position; --i) {
        list[i] = list[i - 1];
    }
    list[position] = *candidate;
    *count = new_count;
}

static bool matches_previous(const component_t *current)
{
    for (size_t i = 0; i < s_previous_count; ++i) {
        int32_t dx = (int32_t)current->x - s_previous[i].x;
        int32_t dy = (int32_t)current->y - s_previous[i].y;
        uint32_t distance_squared = (uint32_t)(dx * dx + dy * dy);
        if (distance_squared <= HOTSPOT_MATCH_RADIUS_PX * HOTSPOT_MATCH_RADIUS_PX) {
            return true;
        }
    }
    return false;
}

esp_err_t app_hotspot_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    s_sort_work = heap_caps_malloc(THERMAL_PIXEL_COUNT * sizeof(uint16_t),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_visited = heap_caps_malloc(THERMAL_PIXEL_COUNT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_queue = heap_caps_malloc(THERMAL_PIXEL_COUNT * sizeof(uint16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_sort_work || !s_visited || !s_queue) {
        ESP_LOGE(TAG, "PSRAM allocation failed for median/flood-fill workspaces");
        heap_caps_free(s_sort_work);
        heap_caps_free(s_visited);
        heap_caps_free(s_queue);
        s_sort_work = NULL;
        s_visited = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_previous_count = 0;
    s_ready = true;
    ESP_LOGI(TAG, "workspace allocated in PSRAM");
    return ESP_OK;
}

void app_hotspot_reset(void)
{
    s_previous_count = 0;
}

esp_err_t app_hotspot_analyze(const uint16_t *pixels, uint16_t width, uint16_t height,
                              hotspot_result_t *result)
{
    if (!s_ready || !pixels || !result) {
        return ESP_ERR_INVALID_STATE;
    }
    if (width != THERMAL_WIDTH || height != THERMAL_HEIGHT) {
        ESP_LOGE(TAG, "unsupported frame size %ux%u", width, height);
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    const size_t count = (size_t)width * height;
    const uint16_t median = image_median(pixels, count);
    result->median_raw = median;
    memset(s_visited, 0, count);

    component_t current[HOTSPOT_MAX_RESULTS];
    size_t current_count = 0;

    for (uint32_t start = 0; start < count; ++start) {
        if (s_visited[start]) {
            continue;
        }
        s_visited[start] = 1;
        if (pixels[start] < median || (uint32_t)pixels[start] - median < HOTSPOT_RAW_DELTA) {
            continue;
        }

        component_t component = {0};
        component.peak_delta = (uint32_t)pixels[start] - median;
        size_t head = 0;
        size_t tail = 0;
        s_queue[tail++] = (uint16_t)start;

        while (head < tail) {
            uint16_t index = s_queue[head++];
            uint16_t x = index % width;
            uint16_t y = index / width;
            uint32_t delta = (uint32_t)pixels[index] - median;
            component.x_sum += x;
            component.y_sum += y;
            ++component.area;
            if (delta > component.peak_delta) {
                component.peak_delta = delta;
            }

            uint32_t neighbors[4];
            size_t neighbor_count = 0;
            if (x > 0) {
                neighbors[neighbor_count++] = index - 1;
            }
            if (x + 1 < width) {
                neighbors[neighbor_count++] = index + 1;
            }
            if (y > 0) {
                neighbors[neighbor_count++] = index - width;
            }
            if (y + 1 < height) {
                neighbors[neighbor_count++] = index + width;
            }
            for (size_t n = 0; n < neighbor_count; ++n) {
                uint32_t next = neighbors[n];
                if (!s_visited[next]) {
                    s_visited[next] = 1;
                    if (pixels[next] >= median &&
                        (uint32_t)pixels[next] - median >= HOTSPOT_RAW_DELTA) {
                        s_queue[tail++] = (uint16_t)next;
                    }
                }
            }
        }

        if (component.area >= HOTSPOT_MIN_AREA_PX) {
            component.x = (uint16_t)(component.x_sum / component.area);
            component.y = (uint16_t)(component.y_sum / component.area);
            insert_component(current, &current_count, &component);
            ++result->candidate_regions;
        }
    }

    hotspot_t confirmed[HOTSPOT_MAX_RESULTS];
    size_t confirmed_count = 0;
    for (size_t i = 0; i < current_count; ++i) {
        if (!matches_previous(&current[i])) {
            continue;
        }
        hotspot_t *item = &confirmed[confirmed_count++];
        item->x = current[i].x;
        item->y = current[i].y;
        item->raw_delta = current[i].peak_delta;
        item->area_px = current[i].area > UINT8_MAX ? UINT8_MAX : (uint8_t)current[i].area;
        float area_score = fminf((float)current[i].area / 16.0f, 1.0f);
        float delta_score = fminf((float)current[i].peak_delta / (2.0f * HOTSPOT_RAW_DELTA), 1.0f);
        item->conf = fminf(0.4f + 0.3f * area_score + 0.3f * delta_score, 1.0f);
        uint16_t peak_raw = (uint16_t)((uint32_t)median + current[i].peak_delta);
        item->temp_c = raw_to_celsius(peak_raw);
        const float baseline_c = raw_to_celsius(median);
        item->delta_c = (isfinite(item->temp_c) && isfinite(baseline_c))
                            ? item->temp_c - baseline_c : NAN;
    }

    for (size_t i = 0; i < confirmed_count && i < HOTSPOT_MAX_RESULTS; ++i) {
        result->items[result->count++] = confirmed[i];
    }
    memcpy(s_previous, current, current_count * sizeof(component_t));
    s_previous_count = current_count;
    return ESP_OK;
}
