#include "encoder.h"
#include "board_config.h"
#include "timebase.h"
#include <stdio.h>

static int32_t g_left_total;
static int32_t g_right_total;
static uint16_t g_left_last;
static uint16_t g_right_last;
#if ENABLE_MOTION_DRIVE
static uint8_t g_initialized;

static void init_timer_encoder(TIM_TypeDef *timer, uint16_t a_pin, GPIO_TypeDef *a_port,
                               uint16_t b_pin, GPIO_TypeDef *b_port)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer_cfg;
    TIM_ICInitTypeDef ic;

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = a_pin | b_pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(a_port, &gpio);
    if (b_port != a_port) {
        GPIO_Init(b_port, &gpio);
    }

    TIM_TimeBaseStructInit(&timer_cfg);
    timer_cfg.TIM_Prescaler = 0U;
    timer_cfg.TIM_Period = 0xFFFFU;
    timer_cfg.TIM_ClockDivision = TIM_CKD_DIV1;
    timer_cfg.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(timer, &timer_cfg);

    TIM_ICStructInit(&ic);
    ic.TIM_Channel = TIM_Channel_1;
    ic.TIM_ICPolarity = TIM_ICPolarity_Rising;
    ic.TIM_ICSelection = TIM_ICSelection_DirectTI;
    ic.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    ic.TIM_ICFilter = ENCODER_IC_FILTER;
    TIM_ICInit(timer, &ic);
    ic.TIM_Channel = TIM_Channel_2;
    TIM_ICInit(timer, &ic);
    TIM_EncoderInterfaceConfig(timer, TIM_EncoderMode_TI12,
                               TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
    TIM_SetCounter(timer, 0U);
    TIM_Cmd(timer, ENABLE);
}

#endif /* ENABLE_MOTION_DRIVE */

void encoder_init(void)
{
#if ENABLE_MOTION_DRIVE
    if (g_initialized) {
        return;
    }
    RCC_APB1PeriphClockCmd(ENCODER_LEFT_TIM_CLK | ENCODER_RIGHT_TIM_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO, ENABLE);
    /* board_init 已统一关闭 JTAG 保留 SWD，PB3/PB4 与 PB6/PB7 才能作 GPIO/TIM 输入。 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);

    init_timer_encoder(ENCODER_LEFT_TIM, ENCODER_LEFT_A_PIN, ENCODER_LEFT_A_PORT,
                       ENCODER_LEFT_B_PIN, ENCODER_LEFT_B_PORT);
    init_timer_encoder(ENCODER_RIGHT_TIM, ENCODER_RIGHT_A_PIN, ENCODER_RIGHT_A_PORT,
                       ENCODER_RIGHT_B_PIN, ENCODER_RIGHT_B_PORT);
    encoder_reset();
    g_initialized = 1U;
#endif
}

void encoder_update(void)
{
#if ENABLE_MOTION_DRIVE
    uint16_t left_now;
    uint16_t right_now;
    int32_t left_delta;
    int32_t right_delta;

    if (!g_initialized) {
        return;
    }
    left_now = TIM_GetCounter(ENCODER_LEFT_TIM);
    right_now = TIM_GetCounter(ENCODER_RIGHT_TIM);
    left_delta = (int16_t)(left_now - g_left_last);
    right_delta = (int16_t)(right_now - g_right_last);
    g_left_total += left_delta;
    g_right_total += right_delta;
    g_left_last = left_now;
    g_right_last = right_now;
#endif
}

void encoder_reset(void)
{
    g_left_total = 0;
    g_right_total = 0;
    g_left_last = 0U;
    g_right_last = 0U;
#if ENABLE_MOTION_DRIVE
    if (g_initialized) {
        TIM_SetCounter(ENCODER_LEFT_TIM, 0U);
        TIM_SetCounter(ENCODER_RIGHT_TIM, 0U);
    }
#endif
}

int32_t encoder_left_count(void)
{
    return g_left_total;
}

int32_t encoder_right_count(void)
{
    return g_right_total;
}

void encoder_test(void)
{
#if ENABLE_MOTION_DRIVE
    int32_t left_start;
    int32_t right_start;
    encoder_update();
    left_start = g_left_total;
    right_start = g_right_total;
    printf("encoder test: rotate wheels for 3 seconds\r\n");
    for (uint32_t elapsed = 0U; elapsed < 3000U; elapsed += 20U) {
        encoder_update();
        timebase_delay_ms(20U);
    }
    printf("encoder test: L %+ld -> %+ld, R %+ld -> %+ld\r\n",
           (long)left_start, (long)g_left_total,
           (long)right_start, (long)g_right_total);
#else
    printf("本功能未启用（采购清单无编码器）\r\n");
#endif
}
