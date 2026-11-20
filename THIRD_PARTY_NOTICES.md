# 第三方组件与许可声明

本项目（`thermal-robot-s31`，MIT 许可）在构建与运行时使用下列第三方组件。
本文件只做归属声明，不改变各组件自身的许可条款。分发本项目源码或二进制时，
请一并保留各组件的原始版权与许可文本。

## 一、随本仓库分发的二进制所包含的组件

`firmware/STM32F103CBT6/` 下的 `.elf` / `.hex` / `.bin` 是用 GNU Arm 工具链编译的 STM32 固件，
其中链接了下列代码。

| 组件 | 版本/来源 | 许可 | 说明 |
|---|---|---|---|
| STM32F10x Standard Peripheral Library | STMicroelectronics SPL V3.5.0 | MCD-ST Liberty SW License Agreement V2（BSD-3 类） | **本仓库不包含其源码**。`main/STM32F103CBT6/Makefile` 通过 `SPL_DIR ?= ../spl` 引用外部目录，使用者需自行从 ST 官方获取并遵守其许可 |
| STM32 CMSIS Device Support（`system_stm32f10x.c`、`startup_stm32f10x_md.s`） | STMicroelectronics CMSIS | 同上（随 SPL 分发） | 同上，源码不在仓库内 |

> 若要分发 `firmware/` 下的二进制，请自行确认已满足上述 ST 许可的声明保留要求。
> 若不确定，删除 `firmware/` 目录后仅分发源码即可规避。

## 二、ESP32-S31 侧依赖（源码不在本仓库）

S31 工程通过 `main/ESP32-S31/main/idf_component.yml` 声明依赖，实际代码存放在构建目录的
`managed_components/` 下，**未纳入本仓库**。

| 组件 | 声明版本 | 许可 | 核实依据 |
|---|---|---|---|
| ESP-IDF | `>=6.1` | Apache-2.0 | 本机 `esp-idf/LICENSE` 首行 |
| `espressif/usb` | `==1.6.0` | Apache-2.0 | 组件内 `LICENSE` 文件 |
| `espressif/esp_codec_dev` | `==1.6.2` | Apache-2.0 | 组件内 `LICENSE` 文件 |
| FreeRTOS-Kernel（随 ESP-IDF） | IDF v6.1 内置 | MIT | `components/freertos/FreeRTOS-Kernel/LICENSE.md` |

## 三、协议信息来源

MAG160Core 热像模块使用**厂商私有 USB Bulk 协议**（VID `0x833C`），不是标准 UVC。
本项目中的命令字、端点号、帧头尾魔数等常量整理自公开的逆向工程资料与社区讨论。

- 这些信息**不是**厂商官方文档，可能存在版本差异。
- 本项目与 MAG160Core 的制造商无任何关联，未获其授权或背书。
- 若厂商提出异议，相关常量定义应移除或替换为官方 SDK。

## 四、本项目自身

| 项 | 内容 |
|---|---|
| 许可 | MIT，全文见仓库根 `LICENSE` |
| 版权人 | `WuJiapeng-dev` |
| 覆盖范围 | `main/ESP32-S31/`、`main/STM32F103CBT6/` 下的自有源码与文档 |
| 不覆盖 | 上述第三方组件、ST 官方库、厂商协议资料 |
