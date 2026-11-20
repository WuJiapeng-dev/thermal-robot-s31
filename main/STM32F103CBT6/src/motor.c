#include "motor.h"
#include "battery.h"
#include "board.h"
#include "board_config.h"
#include "timebase.h"
#include <stdio.h>

#if ENABLE_MOTION_DRIVE

/* TB6612 两路输入真值表（方案说明书 5.5）：正向说明见下。
 *   IN1=1, IN2=0  -> 正转（PWM 调速）
 *   IN1=0, IN2=1  -> 反转
 *   IN1=1, IN2=1  -> 短路制动（PWM 无效）
 *   IN1=0, IN2=0  -> 停止 / 滑行（输出高阻）
 *
 * 早期版本用 `sign >= 0` / `sign <= 0` 从同一个 sign 推导两根脚，导致：
 *   sign = 0 -> 输出 (1,1) 即制动，而不是滑行；
 *   sign = +1 -> 输出 (1,0) 即正转；
 * 于是 motor_brake() 实际输出「正转 + 满占空比」，motor_coast() 实际输出制动，
 * 两个语义完全互换。上电后第一次调用 motor_brake() 会让车体全速前冲。
 * 因此这里改为显式方向枚举，不再从数值符号推导。 */
typedef enum {
    MOTOR_DIR_COAST = 0,
    MOTOR_DIR_FORWARD,
    MOTOR_DIR_REVERSE,
    MOTOR_DIR_BRAKE
} motor_dir_t;

static int16_t g_left;
static int16_t g_right;
static int8_t g_left_sign;
static int8_t g_right_sign;
static uint8_t g_initialized;

static void write_channel(GPIO_TypeDef *in1_port, uint16_t in1_pin,
                          GPIO_TypeDef *in2_port, uint16_t in2_pin,
                          motor_dir_t dir)
{
    switch (dir) {
    case MOTOR_DIR_FORWARD:
        GPIO_WriteBit(in1_port, in1_pin, Bit_SET);
        GPIO_WriteBit(in2_port, in2_pin, Bit_RESET);
        break;
    case MOTOR_DIR_REVERSE:
        GPIO_WriteBit(in1_port, in1_pin, Bit_RESET);
        GPIO_WriteBit(in2_port, in2_pin, Bit_SET);
        break;
    case MOTOR_DIR_BRAKE:
        GPIO_WriteBit(in1_port, in1_pin, Bit_SET);
        GPIO_WriteBit(in2_port, in2_pin, Bit_SET);
        break;
    default: /* MOTOR_DIR_COAST */
        GPIO_WriteBit(in1_port, in1_pin, Bit_RESET);
        GPIO_WriteBit(in2_port, in2_pin, Bit_RESET);
        break;
    }
}

static motor_dir_t dir_from_speed(int16_t value)
{
    if (value > 0) {
        return MOTOR_DIR_FORWARD;
    }
    if (value < 0) {
        return MOTOR_DIR_REVERSE;
    }
    return MOTOR_DIR_COAST;
}

static void write_directions(motor_dir_t left_dir, motor_dir_t right_dir)
{
    /* VM 仅 7.4~8.4V，严禁 12V；STBY 硬件接 3.3V。 */
    write_channel(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN,
                  MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, left_dir);
    write_channel(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN,
                  MOTOR_BIN2_PORT, MOTOR_BIN2_PIN, right_dir);
}

static void brake_both(void)
{
    /* 短路制动：IN1=IN2=1。此时 TB6612 把电机两端短接到地，PWM 不参与，
     * 这里把占空比归零只是避免恢复驱动时残留上一次的比较值。 */
    write_directions(MOTOR_DIR_BRAKE, MOTOR_DIR_BRAKE);
    TIM_SetCompare1(MOTOR_TIM, 0U);
    TIM_SetCompare4(MOTOR_TIM, 0U);
}

static void apply_pwm(int16_t left, int16_t right)
{
    uint32_t battery_mv = battery_read_millivolts();
    /* 高电压时限到 85%；低于 7.4V 后恢复 100%，与方案要求一致。 */
    int32_t limit = (battery_mv >= MOTOR_LIMIT_RESTORE_MV) ?
                    MOTOR_FULL_BATTERY_DUTY_LIMIT : MOTOR_FULL_DUTY_LIMIT;

    if (left > limit) { left = (int16_t)limit; }
    if (left < -limit) { left = (int16_t)(-limit); }
    if (right > limit) { right = (int16_t)limit; }
    if (right < -limit) { right = (int16_t)(-limit); }

    TIM_SetCompare1(MOTOR_TIM, (uint16_t)((left < 0 ? -left : left) *
                                          (MOTOR_PWM_ARR + 1) / 1000));
    TIM_SetCompare4(MOTOR_TIM, (uint16_t)((right < 0 ? -right : right) *
                                          (MOTOR_PWM_ARR + 1) / 1000));
    g_left = left;
    g_right = right;
}

#endif /* ENABLE_MOTION_DRIVE */

void motor_init(void)
{
#if ENABLE_MOTION_DRIVE
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef oc;

    if (g_initialized) {
        return;
    }
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO | MOTOR_TIM_CLK, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = MOTOR_LEFT_PWM_PIN | MOTOR_RIGHT_PWM_PIN;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_BIN1_PIN | MOTOR_BIN2_PIN;
    GPIO_Init(GPIOB, &gpio);
    write_directions(MOTOR_DIR_COAST, MOTOR_DIR_COAST);

    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = MOTOR_PWM_PSC;
    timer.TIM_Period = MOTOR_PWM_ARR;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(MOTOR_TIM, &timer);

    TIM_OCStructInit(&oc);
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_OutputNState = TIM_OutputNState_Disable;
    oc.TIM_Pulse = 0U;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(MOTOR_TIM, &oc);
    TIM_OC4Init(MOTOR_TIM, &oc);
    TIM_CtrlPWMOutputs(MOTOR_TIM, ENABLE);
    TIM_Cmd(MOTOR_TIM, ENABLE);

    g_left_sign = 0;
    g_right_sign = 0;
    g_initialized = 1U;
#endif
}

void motor_set(int16_t left, int16_t right)
{
#if ENABLE_MOTION_DRIVE
    int8_t left_sign = (left > 0) ? 1 : ((left < 0) ? -1 : 0);
    int8_t right_sign = (right > 0) ? 1 : ((right < 0) ? -1 : 0);

    if (!g_initialized) {
        motor_init();
    }
    /* 换向前先短路制动 20ms，避免反向电流冲击驱动管。 */
    if ((left_sign != 0 && g_left_sign != 0 && left_sign != g_left_sign) ||
        (right_sign != 0 && g_right_sign != 0 && right_sign != g_right_sign)) {
        brake_both();
        timebase_delay_ms(MOTOR_REVERSE_BRAKE_MS);
    }
    write_directions(dir_from_speed(left), dir_from_speed(right));
    apply_pwm(left, right);
    g_left_sign = left_sign;
    g_right_sign = right_sign;
#else
    (void)left;
    (void)right;
#endif
}

void motor_brake(void)
{
#if ENABLE_MOTION_DRIVE
    brake_both();
    g_left = 0;
    g_right = 0;
    g_left_sign = 0;
    g_right_sign = 0;
#endif
}

void motor_coast(void)
{
#if ENABLE_MOTION_DRIVE
    write_directions(MOTOR_DIR_COAST, MOTOR_DIR_COAST);
    TIM_SetCompare1(MOTOR_TIM, 0U);
    TIM_SetCompare4(MOTOR_TIM, 0U);
    g_left = 0;
    g_right = 0;
    g_left_sign = 0;
    g_right_sign = 0;
#endif
}

void motor_test(void)
{
#if ENABLE_MOTION_DRIVE
    printf("motor test: forward\r\n");
    motor_set(300, 300);
    timebase_delay_ms(500U);
    printf("motor test: backward\r\n");
    motor_set(-300, -300);
    timebase_delay_ms(500U);
    motor_brake();
    printf("motor test done\r\n");
#else
    printf("本功能未启用（采购清单无驱动板）\r\n");
#endif
}
