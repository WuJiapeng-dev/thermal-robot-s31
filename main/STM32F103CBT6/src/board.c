#include "board.h"
#include "board_config.h"
#include "timebase.h"

static uint32_t g_heartbeat_start;
static uint32_t g_buzzer_start;
static uint16_t g_buzzer_duration;
static uint8_t g_heartbeat_state;
static uint8_t g_buzzer_active;

void board_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);

    /* 必须在任何 GPIO 初始化之前执行：关闭 JTAG、保留 SWD，释放 PB3/PB4。 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    /* PB6/PB7 留给 TIM4；INA219 使用 PB8/PB9。即使运动关闭也先固定重映射。 */
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;

    gpio.GPIO_Pin = HEARTBEAT_PIN;
    GPIO_Init(HEARTBEAT_PORT, &gpio);
    GPIO_ResetBits(HEARTBEAT_PORT, HEARTBEAT_PIN);

    gpio.GPIO_Pin = BUZZER_PIN;
    GPIO_Init(BUZZER_PORT, &gpio);
    GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);

    g_heartbeat_start = timebase_millis();
    g_buzzer_start = 0U;
    g_buzzer_duration = 0U;
    g_heartbeat_state = 0U;
    g_buzzer_active = 0U;
}

void board_background_task(void)
{
    uint32_t now = timebase_millis();

    if (timebase_expired(now, g_heartbeat_start, HEARTBEAT_PERIOD_MS)) {
        g_heartbeat_start = now;
        g_heartbeat_state ^= 1U;
        GPIO_WriteBit(HEARTBEAT_PORT, HEARTBEAT_PIN,
                      g_heartbeat_state ? Bit_SET : Bit_RESET);
    }

    if (g_buzzer_active &&
        timebase_expired(now, g_buzzer_start, g_buzzer_duration)) {
        GPIO_ResetBits(BUZZER_PORT, BUZZER_PIN);
        g_buzzer_active = 0U;
    }
}

void board_buzzer_beep(uint16_t duration_ms)
{
    g_buzzer_start = timebase_millis();
    g_buzzer_duration = duration_ms;
    g_buzzer_active = 1U;
    GPIO_SetBits(BUZZER_PORT, BUZZER_PIN);
}
