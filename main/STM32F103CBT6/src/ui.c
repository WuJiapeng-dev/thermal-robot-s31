#include "ui.h"
#include "ili9341.h"
#include "board_config.h"
#include "battery.h"
#include "timebase.h"
#include <stdio.h>
#include <string.h>

#define UI_TOP_BG       LCD_COLOR_BLACK
#define UI_TOP_FG       LCD_COLOR_CYAN
#define UI_NORMAL_FG    LCD_COLOR_GREEN
#define UI_WARNING_FG   LCD_COLOR_YELLOW
#define UI_ERROR_FG     LCD_COLOR_RED

static char g_group[JSON_GROUP_CHARS];
static char g_zh[JSON_ZH_CHARS];
static char g_status[UI_MAX_TEXT_CHARS];
static int32_t g_id;
static int32_t g_x;
static int32_t g_y;
static int32_t g_area;
static float g_temperature;
static float g_delta_temperature;
static float g_confidence;
static uint32_t g_battery_mv;
static uint8_t g_link_connected;

static void format_temperature(char *out, uint32_t size, float value)
{
    int32_t scaled;
    uint32_t magnitude;
    char sign = ' ';

    if (value == TEMPERATURE_INVALID) {
        (void)snprintf(out, size, "--");
        return;
    }
    if (value < 0.0f) {
        sign = '-';
        value = -value;
    }
    scaled = (int32_t)(value * 10.0f + 0.5f);
    magnitude = (uint32_t)scaled;
    if (sign == '-') {
        (void)snprintf(out, size, "-%lu.%lu",
                       (unsigned long)(magnitude / 10U),
                       (unsigned long)(magnitude % 10U));
    } else {
        (void)snprintf(out, size, "%lu.%lu",
                       (unsigned long)(magnitude / 10U),
                       (unsigned long)(magnitude % 10U));
    }
}

static void render_top(void)
{
    lcd_fill_rect(0U, 0U, LCD_WIDTH, 50U, UI_TOP_BG);
    lcd_draw_string(4U, 2U, g_group, UI_TOP_FG, UI_TOP_BG, 2U);
    lcd_draw_string(4U, 34U, g_zh, LCD_COLOR_WHITE, UI_TOP_BG, 1U);
}

static void render_temperature(void)
{
    char line[UI_MAX_TEXT_CHARS];
    char temperature[20];
    format_temperature(temperature, sizeof(temperature), g_temperature);
    (void)snprintf(line, sizeof(line), "ID:%ld X:%ld Y:%ld T:%sC",
                   (long)g_id, (long)g_x, (long)g_y, temperature);
    lcd_fill_rect(0U, 52U, LCD_WIDTH, 24U, UI_TOP_BG);
    lcd_draw_string(4U, 56U, line, LCD_COLOR_WHITE, UI_TOP_BG, 1U);
}

static void render_status(void)
{
    char line[UI_MAX_TEXT_CHARS + 12U];
    uint16_t color = g_link_connected ? UI_NORMAL_FG : UI_ERROR_FG;
    unsigned int percent = battery_percent(g_battery_mv);
    (void)snprintf(line, sizeof(line), "%s BAT:%u%%", g_status, percent);
    lcd_fill_rect(0U, 82U, LCD_WIDTH, 22U, UI_TOP_BG);
    lcd_draw_string(4U, 88U, line, color, UI_TOP_BG, 1U);
}

static void render_details(void)
{
    char line[UI_MAX_TEXT_CHARS];
    char delta[20];
    int32_t confidence = (int32_t)(g_confidence * 100.0f + 0.5f);
    if (confidence < 0) { confidence = 0; }
    if (confidence > 100) { confidence = 100; }
    format_temperature(delta, sizeof(delta), g_delta_temperature);
    (void)snprintf(line, sizeof(line), "DT:%sC CF:%ld%% PX:%ld",
                   delta, (long)confidence, (long)g_area);
    lcd_fill_rect(0U, 106U, LCD_WIDTH, 20U, UI_TOP_BG);
    lcd_draw_string(4U, 110U, line, UI_TOP_FG, UI_TOP_BG, 1U);
}

void ui_init(void)
{
    (void)strncpy(g_group, "WAIT S31", sizeof(g_group) - 1U);
    g_group[sizeof(g_group) - 1U] = '\0';
    (void)strncpy(g_zh, "等待连接", sizeof(g_zh) - 1U);
    g_zh[sizeof(g_zh) - 1U] = '\0';
    (void)strncpy(g_status, "WAIT HB", sizeof(g_status) - 1U);
    g_status[sizeof(g_status) - 1U] = '\0';
    g_id = -1;
    g_x = 0;
    g_y = 0;
    g_area = 0;
    g_temperature = TEMPERATURE_INVALID;
    g_delta_temperature = TEMPERATURE_INVALID;
    g_confidence = 0.0f;
    g_battery_mv = 0U;
    g_link_connected = 1U;
    lcd_init();
    ui_refresh_all();
}

void ui_refresh_all(void)
{
    lcd_fill_screen(UI_TOP_BG);
    render_top();
    render_temperature();
    render_status();
    render_details();
}

void ui_show_hot(const json_frame_t *frame)
{
    if (frame == 0) { return; }
    (void)strncpy(g_group, frame->grp, sizeof(g_group) - 1U);
    g_group[sizeof(g_group) - 1U] = '\0';
    (void)strncpy(g_zh, frame->zh, sizeof(g_zh) - 1U);
    g_zh[sizeof(g_zh) - 1U] = '\0';
    g_id = frame->id;
    g_x = frame->x;
    g_y = frame->y;
    g_area = frame->px;
    g_temperature = frame->t;
    g_delta_temperature = frame->dt;
    g_confidence = frame->conf;
    (void)snprintf(g_status, sizeof(g_status), "HOT 过热");
    render_top();
    render_temperature();
    render_status();
    render_details();
}

void ui_show_stage(const char *stage, int32_t progress)
{
    if (stage == 0) { return; }
    (void)snprintf(g_status, sizeof(g_status), "%s %ld%%", stage, (long)progress);
    render_status();
}

void ui_set_link(uint8_t connected)
{
    if (g_link_connected == connected) { return; }
    g_link_connected = connected;
    if (connected) {
        (void)snprintf(g_status, sizeof(g_status), "LINK OK 正常");
    } else {
        (void)snprintf(g_status, sizeof(g_status), "S31 断链");
    }
    render_status();
}

void ui_set_battery(uint32_t millivolts)
{
    if (g_battery_mv == millivolts) { return; }
    g_battery_mv = millivolts;
    if (battery_is_undervoltage(millivolts)) {
        (void)snprintf(g_status, sizeof(g_status), "LOW BAT 欠压");
    }
    render_status();
}

void ui_test(void)
{
    lcd_test_pattern();
    timebase_delay_ms(800U);
    ui_refresh_all();
}
