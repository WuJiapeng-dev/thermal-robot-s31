#include "debug_console.h"
#include "battery.h"
#include "board.h"
#include "board_config.h"
#include "comm.h"
#include "encoder.h"
#include "ili9341.h"
#include "json_frame.h"
#include "motor.h"
#include "motion.h"
#include "ui.h"
#include <stdlib.h>
#include <string.h>

static char g_history[LINK_HISTORY_COUNT][COMM_RX_FRAME_MAX + 1U];
static uint16_t g_history_length[LINK_HISTORY_COUNT];
static uint8_t g_history_next;

void debug_write_byte(uint8_t byte)
{
    uint32_t timeout = UART_TX_TIMEOUT;
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_TXE) == RESET) {
        if (--timeout == 0U) { return; }
    }
    USART_SendData(DEBUG_USART, byte);
}

int fputc(int ch, FILE *file)
{
    (void)file;
    debug_write_byte((uint8_t)ch);
    return ch;
}

void link_history_push(const char *frame, uint16_t length)
{
    uint8_t index;
    if (frame == 0 || length == 0U || length > COMM_RX_FRAME_MAX) {
        return;
    }
    index = g_history_next;
    memcpy(g_history[index], frame, length);
    g_history[index][length] = '\0';
    g_history_length[index] = length;
    g_history_next = (uint8_t)((index + 1U) % LINK_HISTORY_COUNT);
}

void link_history_print(void)
{
    uint8_t i;
    printf("last %u S31 frames:\r\n", (unsigned)LINK_HISTORY_COUNT);
    for (i = 0U; i < LINK_HISTORY_COUNT; ++i) {
        uint8_t index = (uint8_t)((g_history_next + LINK_HISTORY_COUNT - 1U - i) %
                                  LINK_HISTORY_COUNT);
        if (g_history_length[index] != 0U) {
            printf("  [%u] %s\r\n", (unsigned)(LINK_HISTORY_COUNT - i),
                   g_history[index]);
        }
    }
    printf("drops=%lu oversize=%lu\r\n",
           (unsigned long)comm_drop_count(), (unsigned long)comm_oversize_count());
}

void debug_console_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | DEBUG_USART_CLK, ENABLE);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = DEBUG_TX_PIN;
    GPIO_Init(DEBUG_TX_PORT, &gpio);
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Pin = DEBUG_RX_PIN;
    GPIO_Init(DEBUG_RX_PORT, &gpio);

    USART_StructInit(&usart);
    usart.USART_BaudRate = DEBUG_BAUD;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(DEBUG_USART, &usart);
    USART_Cmd(DEBUG_USART, ENABLE);
    memset(g_history, 0, sizeof(g_history));
    memset(g_history_length, 0, sizeof(g_history_length));
    g_history_next = 0U;
    printf("\r\nPCB thermal robot STM32F103CBT6 console\r\n");
}

#if !ENABLE_MOTION_DRIVE
static void print_motion_disabled(void)
{
    printf("本功能未启用（采购清单无驱动板）\r\n");
}
#endif

static void handle_command(char *line)
{
    char *command = strtok(line, " \t\r\n");
    char *arg1;
    if (command == 0) {
        return;
    }
    arg1 = strtok(0, " \t\r\n");
    (void)arg1;

    if (strcmp(command, "help") == 0) {
        printf("help/lcdtest/link/beep/bat/mtest/etest/go <mm>/turn <deg>/scan [step] [dwell]\r\n");
    } else if (strcmp(command, "lcdtest") == 0) {
        ui_test();
        printf("lcdtest done\r\n");
    } else if (strcmp(command, "link") == 0) {
        link_history_print();
    } else if (strcmp(command, "beep") == 0) {
        board_buzzer_beep(BUZZER_KEY_MS);
        printf("beep\r\n");
    } else if (strcmp(command, "bat") == 0) {
        uint32_t mv = battery_read_millivolts();
        printf("BAT: %lu mV, %u%%\r\n", (unsigned long)mv,
               (unsigned)battery_percent(mv));
    } else if (strcmp(command, "mtest") == 0) {
#if ENABLE_MOTION_DRIVE
        motor_test();
#else
        print_motion_disabled();
#endif
    } else if (strcmp(command, "etest") == 0) {
#if ENABLE_MOTION_DRIVE
        encoder_test();
#else
        print_motion_disabled();
#endif
    } else if (strcmp(command, "go") == 0) {
#if ENABLE_MOTION_DRIVE
        int32_t mm = arg1 ? (int32_t)strtol(arg1, 0, 10) : 50;
        printf("go %ld mm: %s\r\n", (long)mm,
               motion_move_mm(mm) ? "OK" : "FAIL");
#else
        print_motion_disabled();
#endif
    } else if (strcmp(command, "turn") == 0) {
#if ENABLE_MOTION_DRIVE
        int32_t deg = arg1 ? (int32_t)strtol(arg1, 0, 10) : 90;
        printf("turn %ld deg: %s\r\n", (long)deg,
               motion_turn_deg(deg) ? "OK" : "FAIL");
#else
        print_motion_disabled();
#endif
    } else if (strcmp(command, "scan") == 0) {
#if ENABLE_MOTION_DRIVE
        int32_t step = arg1 ? (int32_t)strtol(arg1, 0, 10) : MOTION_DEFAULT_STEP_MM;
        char *arg2 = strtok(0, " \t\r\n");
        uint32_t dwell = arg2 ? (uint32_t)strtoul(arg2, 0, 10) : MOTION_DEFAULT_DWELL_MS;
        printf("scan step=%ld dwell=%lums: %s\r\n", (long)step,
               (unsigned long)dwell,
               motion_step_scan(step, dwell) ? "OK" : "FAIL");
#else
        print_motion_disabled();
#endif
    } else {
        printf("unknown command: %s\r\n", command);
    }
}

void debug_console_poll(void)
{
    static char line[DEBUG_LINE_MAX];
    static uint8_t used;
    while (USART_GetFlagStatus(DEBUG_USART, USART_FLAG_RXNE) != RESET) {
        char ch = (char)(USART_ReceiveData(DEBUG_USART) & 0xFFU);
        if (ch == '\r' || ch == '\n') {
            if (used != 0U) {
                line[used] = '\0';
                printf("\r\n");
                handle_command(line);
                used = 0U;
            }
        } else if (used + 1U < sizeof(line)) {
            line[used++] = ch;
        } else {
            printf("\r\nline too long\r\n");
            used = 0U;
        }
    }
}
