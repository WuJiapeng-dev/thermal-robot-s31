#include "app.h"
#include "board.h"
#include "board_config.h"
#include "comm.h"
#include "debug_console.h"
#include "ili9341.h"
#include "json_frame.h"
#include "timebase.h"
#include "ui.h"
#include "battery.h"
#include <stdio.h>

static uint32_t g_last_heartbeat;
static uint32_t g_last_battery_poll;
static uint8_t g_link_up;
static uint32_t g_parse_fail_count;

void app_init(void)
{
    timebase_init();
    board_init();
    battery_init();
    comm_init();
    ui_init();
    debug_console_init();

    g_last_heartbeat = timebase_millis();
    g_last_battery_poll = 0U;
    g_link_up = 1U;
    g_parse_fail_count = 0U;
    comm_send_ready();
    printf("STM32 side ready\r\n");
}

void app_link_poll(void)
{
    uint16_t length;
    const char *text = comm_acquire_frame(&length);

    if (text == 0) {
        return;
    }
    link_history_push(text, length);

    {
        json_frame_t frame;
        if (!json_parse_frame(text, (uint32_t)length, &frame)) {
            ++g_parse_fail_count;
            printf("JSON parse failed: %s\r\n", text);
        } else {
            g_last_heartbeat = timebase_millis();
            if (!g_link_up) {
                g_link_up = 1U;
            }
            ui_set_link(1U);

            if (frame.type == JSON_TYPE_HOT) {
                if (frame.missing_mask != 0U) {
                    printf("hot frame defaults used, missing_mask=0x%08lX\r\n",
                           (unsigned long)frame.missing_mask);
                }
                ui_show_hot(&frame);
                board_buzzer_beep(BUZZER_EVENT_MS);
                comm_send_ack(frame.id);
            } else if (frame.type == JSON_TYPE_ST) {
                ui_show_stage(frame.stage, frame.prog);
            } else if (frame.type == JSON_TYPE_CAPTURE_DONE) {
                if (frame.missing_mask == 0U) {
                    comm_mark_capture_done(frame.seq, frame.frames);
                } else {
                    (void)printf("capture_done missing fields: 0x%08lX\r\n",
                                 (unsigned long)frame.missing_mask);
                }
                ui_show_stage("done", 100);
            } else {
                /* heartbeat：只刷新断链计时，热点数量由下一次 hot/st 帧更新。 */
            }
        }
    }
    comm_release_frame();
}

void app_periodic_poll(void)
{
    uint32_t now = timebase_millis();
    board_background_task();

    if (timebase_expired(now, g_last_battery_poll, BATTERY_REFRESH_MS)) {
        uint32_t millivolts = battery_read_millivolts();
        g_last_battery_poll = now;
        ui_set_battery(millivolts);
    }

    if (timebase_expired(now, g_last_heartbeat, LINK_TIMEOUT_MS)) {
        if (g_link_up) {
            g_link_up = 0U;
            ui_set_link(0U);
            board_buzzer_beep(BUZZER_EVENT_MS);
            printf("S31 link timeout\r\n");
        }
    }
}

int main(void)
{
    app_init();
    while (1) {
        app_link_poll();
        app_periodic_poll();
        debug_console_poll();
    }
}
