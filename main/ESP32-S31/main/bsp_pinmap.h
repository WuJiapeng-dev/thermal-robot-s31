#pragma once

#include <driver/gpio.h>
#include <driver/uart.h>

/* Console UART0: fixed by the board and IDF; this project does not remap it. */
#define PINMAP_CONSOLE_UART             UART_NUM_0
#define PINMAP_CONSOLE_TX               GPIO_NUM_58
#define PINMAP_CONSOLE_RX               GPIO_NUM_59
#define PINMAP_RGB_LED                  GPIO_NUM_60  /* Reserved, not driven. */
#define PINMAP_BOOT_BUTTON              GPIO_NUM_61  /* Reserved, not driven. */

/* TF card: ESP32-S31 SDMMC IOMUX fixed pins. */
#define PINMAP_SD_CLK                   GPIO_NUM_24
#define PINMAP_SD_CMD                   GPIO_NUM_25
#define PINMAP_SD_D0                    GPIO_NUM_20
#define PINMAP_SD_D1                    GPIO_NUM_21
#define PINMAP_SD_D2                    GPIO_NUM_22
#define PINMAP_SD_D3                    GPIO_NUM_23

/* MAG160Core uses native USB D+/D-, so it consumes no GPIO here. */

/* OV2640 DVP, plan B. D5/D6/D7 are deliberately moved off GPIO0/45/46. */
#define PINMAP_CAM_XCLK                 GPIO_NUM_48
#define PINMAP_CAM_PCLK                 GPIO_NUM_42
#define PINMAP_CAM_VSYNC                GPIO_NUM_43
#define PINMAP_CAM_HREF                 GPIO_NUM_44
#define PINMAP_CAM_SIOD                 GPIO_NUM_47
#define PINMAP_CAM_SIOC                 GPIO_NUM_49
#define PINMAP_CAM_D0                   GPIO_NUM_38
#define PINMAP_CAM_D1                   GPIO_NUM_39
#define PINMAP_CAM_D2                   GPIO_NUM_40
#define PINMAP_CAM_D3                   GPIO_NUM_2
#define PINMAP_CAM_D4                   GPIO_NUM_3
#define PINMAP_CAM_D5                   GPIO_NUM_1
#define PINMAP_CAM_D6                   GPIO_NUM_35
#define PINMAP_CAM_D7                   GPIO_NUM_36
#define PINMAP_CAM_XCLK_HZ              20000000
#define PINMAP_CAM_I2C_PORT             I2C_NUM_1

/* SYN6288: 3.3 V logic only. A 5 V RX input can damage the S31 GPIO. */
#define PINMAP_TTS_UART                 UART_NUM_1
#define PINMAP_TTS_TX                   GPIO_NUM_4

/* S31 TX -> STM32 PA3; S31 RX <- STM32 PA2; cross-connect and share ground. */
#define PINMAP_LINK_UART                UART_NUM_2
#define PINMAP_LINK_UART_TX             GPIO_NUM_46 /* Strapping: ROM log select; change this one macro if needed. */
#define PINMAP_LINK_UART_RX             GPIO_NUM_37

/* Board-owned ES8311 + on-board microphone + NS4150B, from xiaozhi board README/config. */
#define PINMAP_AUDIO_I2C_PORT           I2C_NUM_0
#define PINMAP_AUDIO_I2C_SCL            GPIO_NUM_50
#define PINMAP_AUDIO_I2C_SDA            GPIO_NUM_51
#define PINMAP_AUDIO_I2S_MCLK           GPIO_NUM_52
#define PINMAP_AUDIO_I2S_BCLK           GPIO_NUM_53
#define PINMAP_AUDIO_I2S_DIN            GPIO_NUM_54
#define PINMAP_AUDIO_I2S_WS             GPIO_NUM_55
#define PINMAP_AUDIO_I2S_DOUT           GPIO_NUM_56
#define PINMAP_AUDIO_PA                 GPIO_NUM_57
#define PINMAP_AUDIO_I2S_PORT           I2S_NUM_0

/* Optional external INMP441 wiring is retained but disabled in plan B. */
#define AUDIO_USE_EXTERNAL_I2S_MIC      0
#define PINMAP_EXT_MIC_BCLK             GPIO_NUM_35
#define PINMAP_EXT_MIC_WS               GPIO_NUM_36
#define PINMAP_EXT_MIC_DIN              GPIO_NUM_37
