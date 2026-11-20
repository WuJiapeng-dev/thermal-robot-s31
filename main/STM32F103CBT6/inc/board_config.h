#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "stm32f10x.h"

/* 默认保持安全关闭；装配电机、驱动板、编码器和 INA219 后可由构建参数覆盖为 1。 */
#ifndef ENABLE_MOTION_DRIVE
#define ENABLE_MOTION_DRIVE             0
#endif

/* 时钟与基础周期：外部 HSE 8MHz，经 PLL ×9 得到 72MHz。 */
#define HSE_CLOCK_HZ                    8000000U
#define SYSCLK_HZ                       72000000U
#define SYSTICK_HZ                      1000U
#define HEARTBEAT_PERIOD_MS             500U
#define LINK_TIMEOUT_MS                 3000U
#define BUZZER_EVENT_MS                 120U
#define BUZZER_KEY_MS                   35U
#define BATTERY_REFRESH_MS              1000U

/* S31 通信：PA2/PA3，921600 8N1；72MHz/78=923076，误差约 0.16%。 */
#define COMM_USART                      USART2
#define COMM_USART_CLK                  RCC_APB1Periph_USART2
#define COMM_USART_IRQn                 USART2_IRQn
#define COMM_BAUD                       921600U
#define COMM_TX_PORT                    GPIOA
#define COMM_TX_PIN                     GPIO_Pin_2
#define COMM_RX_PORT                    GPIOA
#define COMM_RX_PIN                     GPIO_Pin_3
#define COMM_DMA                        DMA1
#define COMM_DMA_CLK                    RCC_AHBPeriph_DMA1
#define COMM_DMA_CHANNEL                DMA1_Channel6
#define COMM_RX_FRAME_MAX               512U
#define COMM_RX_BUFFER_COUNT            2U
#define LINK_HISTORY_COUNT              5U

/* USART1 调试台：PA9/PA10，115200 8N1。 */
#define DEBUG_USART                     USART1
#define DEBUG_USART_CLK                 RCC_APB2Periph_USART1
#define DEBUG_BAUD                      115200U
#define DEBUG_TX_PORT                   GPIOA
#define DEBUG_TX_PIN                    GPIO_Pin_9
#define DEBUG_RX_PORT                   GPIOA
#define DEBUG_RX_PIN                    GPIO_Pin_10
#define DEBUG_LINE_MAX                  96U
#define DEBUG_PRINTF_MAX                192U
#define UART_TX_TIMEOUT                 100000U

/* ILI9341：SPI1 18MHz Mode0，只写；PA6/MISO 保持空闲。 */
#define LCD_SPI                         SPI1
#define LCD_SPI_CLK                     RCC_APB2Periph_SPI1
#define LCD_SPI_BAUD_DIV                SPI_BaudRatePrescaler_4
#define LCD_WIDTH                       240U
#define LCD_HEIGHT                      320U
#define LCD_LINE_BUFFER_BYTES           (LCD_WIDTH * 2U)
#define LCD_SCK_PORT                    GPIOA
#define LCD_SCK_PIN                     GPIO_Pin_5
#define LCD_MOSI_PORT                   GPIOA
#define LCD_MOSI_PIN                    GPIO_Pin_7
#define LCD_CS_PORT                     GPIOB
#define LCD_CS_PIN                      GPIO_Pin_12
#define LCD_DC_PORT                     GPIOB
#define LCD_DC_PIN                      GPIO_Pin_13
#define LCD_RST_PORT                    GPIOB
#define LCD_RST_PIN                     GPIO_Pin_14
#define LCD_BL_PORT                     GPIOB
#define LCD_BL_PIN                      GPIO_Pin_15
#define LCD_RESET_LOW_MS                20U
#define LCD_RESET_HIGH_MS               120U
#define LCD_SPI_TIMEOUT                 100000U

/* 电池 ADC：PA4，100k/33k 分压；8.4V 对应约 2.082V。 */
#define BATTERY_ADC                     ADC1
#define BATTERY_ADC_CLK                 RCC_APB2Periph_ADC1
#define BATTERY_ADC_CHANNEL             ADC_Channel_4
#define BATTERY_PORT                    GPIOA
#define BATTERY_PIN                     GPIO_Pin_4
#define ADC_VDDA_MV                     3300U
#define ADC_FULL_SCALE                  4095U
#define BATTERY_DIVIDER_TOP_KOHM        100U
#define BATTERY_DIVIDER_BOTTOM_KOHM     33U
#define BATTERY_MEDIAN_SAMPLES          9U
#define BATTERY_UNDERVOLTAGE_MV         6400U
#define BATTERY_FULL_MV                 8400U
#define BATTERY_EMPTY_MV                6400U
#define ADC_CONVERSION_TIMEOUT          10000U

/* 心跳灯与蜂鸣器。 */
#define HEARTBEAT_PORT                  GPIOA
#define HEARTBEAT_PIN                   GPIO_Pin_12
#define BUZZER_PORT                     GPIOB
#define BUZZER_PIN                      GPIO_Pin_10

/* TB6612：TIM1_CH1/CH4，10kHz。VM 仅接 7.4~8.4V，严禁 12V；STBY 硬件接 3.3V。 */
#define MOTOR_TIM                       TIM1
#define MOTOR_TIM_CLK                   RCC_APB2Periph_TIM1
#define MOTOR_PWM_HZ                    10000U
#define MOTOR_PWM_PSC                   71U
#define MOTOR_PWM_ARR                   99U
#define MOTOR_LEFT_PWM_PORT             GPIOA
#define MOTOR_LEFT_PWM_PIN              GPIO_Pin_8
#define MOTOR_RIGHT_PWM_PORT            GPIOA
#define MOTOR_RIGHT_PWM_PIN             GPIO_Pin_11
#define MOTOR_AIN1_PORT                 GPIOB
#define MOTOR_AIN1_PIN                  GPIO_Pin_3
#define MOTOR_AIN2_PORT                 GPIOB
#define MOTOR_AIN2_PIN                  GPIO_Pin_4
#define MOTOR_BIN1_PORT                 GPIOB
#define MOTOR_BIN1_PIN                  GPIO_Pin_5
#define MOTOR_BIN2_PORT                 GPIOB
#define MOTOR_BIN2_PIN                  GPIO_Pin_0
#define MOTOR_REVERSE_BRAKE_MS          20U
#define MOTOR_FULL_DUTY_LIMIT           1000
#define MOTOR_FULL_BATTERY_DUTY_LIMIT   850
#define MOTOR_LIMIT_RESTORE_MV          7400U

/* 编码器：TIM2/TIM4 TI12，IC 滤波 0xF；下列机械常量全部待实测。 */
#define ENCODER_LEFT_TIM                TIM2
#define ENCODER_LEFT_TIM_CLK            RCC_APB1Periph_TIM2
#define ENCODER_LEFT_A_PORT             GPIOA
#define ENCODER_LEFT_A_PIN              GPIO_Pin_0
#define ENCODER_LEFT_B_PORT             GPIOA
#define ENCODER_LEFT_B_PIN              GPIO_Pin_1
#define ENCODER_RIGHT_TIM               TIM4
#define ENCODER_RIGHT_TIM_CLK           RCC_APB1Periph_TIM4
#define ENCODER_RIGHT_A_PORT            GPIOB
#define ENCODER_RIGHT_A_PIN             GPIO_Pin_6
#define ENCODER_RIGHT_B_PORT            GPIOB
#define ENCODER_RIGHT_B_PIN             GPIO_Pin_7
#define ENCODER_IC_FILTER               0x0FU
#define ENCODER_PULSES_PER_REV          6600       /* 11PPR×4×150，待实测 */
#define ENCODER_WHEEL_CIRCUMFERENCE_MM  157.1f     /* 待实测 */
#define ENCODER_MM_PER_PULSE            0.0238f    /* 待实测 */
#define ENCODER_PULSES_PER_MM           42         /* 待实测 */
#define ENCODER_WHEEL_TRACK_MM          110.0f     /* 待实测 */
#define ENCODER_TURN_90_PULSES          3630       /* 待实测 */

/* 运动控制初值均待实车整定。 */
#define MOTION_CONTROL_PERIOD_MS        20U
#define MOTION_RAMP_MS                  200U
#define MOTION_MAX_SPEED_PPS            600
#define MOTION_MIN_SPEED_PPS            80
#define MOTION_PI_KP_NUM                2
#define MOTION_PI_KP_DEN                1
#define MOTION_PI_KI_NUM                1
#define MOTION_PI_KI_DEN                10
#define MOTION_INTEGRAL_LIMIT           1000
#define MOTION_POSITION_TOLERANCE       8
#define MOTION_DECEL_PULSES             120
#define MOTION_DEFAULT_STEP_MM          5
#define MOTION_DEFAULT_DWELL_MS         500U
#define MOTION_CAPTURE_FRAMES           2U
#define MOTION_CAPTURE_TIMEOUT_MS       8000U
#define MOTION_TIMEOUT_EXTRA_MS         5000U

/* INA219：I2C1 重映射到 PB8/PB9，400kHz，0.1Ω 分流（阻值待采购确认）。 */
#define INA219_I2C                      I2C1
#define INA219_I2C_CLK                  RCC_APB1Periph_I2C1
#define INA219_I2C_SPEED                400000U
#define INA219_SCL_PORT                 GPIOB
#define INA219_SCL_PIN                  GPIO_Pin_8
#define INA219_SDA_PORT                 GPIOB
#define INA219_SDA_PIN                  GPIO_Pin_9
#define INA219_ADDRESS_7BIT             0x40U
#define INA219_SHUNT_OHMS               0.1f
#define INA219_CURRENT_LSB_MA           0.1f
#define INA219_CALIBRATION              4096U
#define INA219_CONFIG                   0x399FU
#define I2C_TIMEOUT                     10000U

/* JSON 与界面：温度 -273.0 是未标定哨兵；无已验证字形时中文显示 ?。 */
#define JSON_GROUP_CHARS                32U
#define JSON_ZH_CHARS                   48U
#define JSON_STAGE_CHARS                16U
#define TEMPERATURE_INVALID             (-273.0f)
#define UI_MAX_TEXT_CHARS               96U
#define FONT12_BYTES                    24U
#define FONT12_CATALOG_TARGET           60U

#endif
