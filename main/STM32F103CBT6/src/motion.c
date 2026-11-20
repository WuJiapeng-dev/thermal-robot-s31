#include "motion.h"
#include "app.h"
#include "battery.h"
#include "board.h"
#include "board_config.h"
#include "comm.h"
#include "encoder.h"
#include "motor.h"
#include "timebase.h"

#if ENABLE_MOTION_DRIVE
static uint8_t g_initialized;
static int32_t g_integral_left;
static int32_t g_integral_right;
static int32_t g_last_left;
static int32_t g_last_right;
static uint32_t g_capture_sequence;

static int32_t clamp32(int32_t value, int32_t limit)
{
    if (value > limit) { return limit; }
    if (value < -limit) { return -limit; }
    return value;
}

static int16_t pi_update(int32_t target_pps, int32_t actual_pps,
                         int32_t *integral, int32_t *last_actual)
{
    int32_t error = target_pps - actual_pps;
    int32_t derivative;
    int32_t output;

    *integral = clamp32(*integral + error, MOTION_INTEGRAL_LIMIT);
    derivative = actual_pps - *last_actual;
    *last_actual = actual_pps;
    output = (MOTION_PI_KP_NUM * error) / MOTION_PI_KP_DEN +
             (MOTION_PI_KI_NUM * (*integral)) / MOTION_PI_KI_DEN -
             derivative / 2;
    return (int16_t)clamp32(output, 1000);
}

static int32_t target_speed(int32_t target, int32_t done, uint32_t elapsed_ms)
{
    int32_t remaining = target - done;
    uint32_t magnitude = remaining < 0 ? (uint32_t)(-remaining) : (uint32_t)remaining;
    int32_t speed = MOTION_MAX_SPEED_PPS;

    if (magnitude < (uint32_t)MOTION_DECEL_PULSES) {
        speed = MOTION_MIN_SPEED_PPS;
    } else if (elapsed_ms < MOTION_RAMP_MS) {
        speed = MOTION_MIN_SPEED_PPS +
                (int32_t)((uint32_t)(MOTION_MAX_SPEED_PPS - MOTION_MIN_SPEED_PPS) *
                          elapsed_ms / MOTION_RAMP_MS);
    }
    return remaining < 0 ? -speed : speed;
}

static uint8_t drive_to(int32_t left_target, int32_t right_target)
{
    uint32_t start = timebase_millis();
    uint32_t last_control = start;
    uint32_t max_time;
    int32_t previous_left = 0;
    int32_t previous_right = 0;

    if (battery_is_undervoltage(battery_read_millivolts())) {
        motor_brake();
        return 0U;
    }
    g_integral_left = 0;
    g_integral_right = 0;
    g_last_left = 0;
    g_last_right = 0;
    max_time = (uint32_t)((left_target < 0 ? -left_target : left_target) *
                          1000U / (uint32_t)MOTION_MIN_SPEED_PPS) +
               MOTION_TIMEOUT_EXTRA_MS;

    while (1) {
        uint32_t now = timebase_millis();
        int32_t left_done;
        int32_t right_done;
        int32_t left_error;
        int32_t right_error;

        board_background_task();
        app_link_poll();
        encoder_update();
        left_done = encoder_left_count();
        right_done = encoder_right_count();
        left_error = left_target - left_done;
        right_error = right_target - right_done;

        if ((left_error > -MOTION_POSITION_TOLERANCE && left_error < MOTION_POSITION_TOLERANCE) &&
            (right_error > -MOTION_POSITION_TOLERANCE && right_error < MOTION_POSITION_TOLERANCE)) {
            motor_brake();
            return 1U;
        }
        if (timebase_expired(now, start, max_time)) {
            motor_brake();
            return 0U;
        }
        if ((uint32_t)(now - last_control) >= MOTION_CONTROL_PERIOD_MS) {
            uint32_t elapsed = now - start;
            uint32_t control_elapsed = now - last_control;
            int32_t left_speed = target_speed(left_target, left_done, elapsed);
            int32_t right_speed = target_speed(right_target, right_done, elapsed);
            int32_t left_actual = ((left_done - previous_left) * 1000) / (int32_t)control_elapsed;
            int32_t right_actual = ((right_done - previous_right) * 1000) / (int32_t)control_elapsed;
            int16_t left_pwm = pi_update(left_speed, left_actual,
                                         &g_integral_left, &g_last_left);
            int16_t right_pwm = pi_update(right_speed, right_actual,
                                          &g_integral_right, &g_last_right);
            previous_left = left_done;
            previous_right = right_done;
            motor_set(left_pwm, right_pwm);
            last_control = now;
        }
    }
}

#endif /* ENABLE_MOTION_DRIVE */

void motion_init(void)
{
#if ENABLE_MOTION_DRIVE
    if (!g_initialized) {
        motor_init();
        encoder_init();
        g_initialized = 1U;
    }
#endif
}

uint8_t motion_move_mm(int32_t distance_mm)
{
#if ENABLE_MOTION_DRIVE
    int32_t target;
    if (!g_initialized) {
        motion_init();
    }
    target = distance_mm * ENCODER_PULSES_PER_MM;
    encoder_reset();
    return drive_to(target, target);
#else
    (void)distance_mm;
    return 0U;
#endif
}

uint8_t motion_turn_deg(int32_t angle_deg)
{
#if ENABLE_MOTION_DRIVE
    int32_t target;
    if (!g_initialized) {
        motion_init();
    }
    target = angle_deg * ENCODER_TURN_90_PULSES / 90;
    encoder_reset();
    return drive_to(target, -target);
#else
    (void)angle_deg;
    return 0U;
#endif
}

uint8_t motion_step_scan(int32_t step_mm, uint32_t dwell_ms)
{
#if ENABLE_MOTION_DRIVE
    uint32_t start;
    if (!motion_move_mm(step_mm)) {
        return 0U;
    }

    /* 先等机械振动稳定，再请求 S31 采两帧；等待期间持续处理串口与心跳灯。 */
    start = timebase_millis();
    while (!timebase_expired(timebase_millis(), start, dwell_ms)) {
        board_background_task();
        app_link_poll();
    }

    ++g_capture_sequence;
    if (g_capture_sequence == 0U) {
        ++g_capture_sequence;
    }
    comm_send_capture(g_capture_sequence, MOTION_CAPTURE_FRAMES);
    start = timebase_millis();
    while (!comm_capture_completed(g_capture_sequence) &&
           !timebase_expired(timebase_millis(), start, MOTION_CAPTURE_TIMEOUT_MS)) {
        board_background_task();
        app_link_poll();
    }
    return (comm_capture_completed(g_capture_sequence) &&
            comm_capture_frames() >= (int32_t)MOTION_CAPTURE_FRAMES) ? 1U : 0U;
#else
    (void)step_mm;
    (void)dwell_ms;
    return 0U;
#endif
}

void motion_stop(void)
{
#if ENABLE_MOTION_DRIVE
    motor_brake();
#endif
}
