# STM32F103CBT6 工程建工程清单（GNU Arm + Keil MDK5）

## 0. GNU Arm 已验证构建（2026-11-19）

本目录新增 `Makefile` 和 `stm32f103cb.ld`，已在 Linux 云主机用 `arm-none-eabi-gcc 10.3.1` 完整构建。将 SPL V3.5.0 放在本目录同级的 `../spl`，或显式传 `SPL_DIR=/path/to/spl`：

```sh
make clean
make -j3
```

默认产物在 `build/`，保持 `ENABLE_MOTION_DRIVE=0`。仅检查运动代码编译时使用独立目录，不覆盖默认固件：

```sh
make -j3 BUILD_DIR=build_motion EXTRA_CFLAGS=-DENABLE_MOTION_DRIVE=1
```

两种配置均为 0 error / 0 warning。默认 `text/data/bss = 37820/500/7052`；运动配置为 `42324/500/7100`。这只是 GNU Arm 编译和链接验证，不替代 Keil 验证或实机测试。

---

目标：STM32F103CBT6，LQFP48，128KB Flash，20KB RAM，72MHz。  
库：STM32F10x Standard Peripheral Library V3.5.0。  
本目录不提供 `.uvprojx/.uvoptx`，避免绝对路径绑定；按下列清单在 Keil 中新建工程。

## 1. 工程宏定义

- `USE_STDPERIPH_DRIVER`
- `STM32F10X_MD`

## 2. 本工程源文件（加入 User/Source Group）

- `src/app.c`
- `src/board.c`
- `src/timebase.c`
- `src/stm32f10x_it.c`
- `src/debug_console.c`
- `src/comm.c`
- `src/json_frame.c`
- `src/ui.c`
- `src/ili9341.c`
- `src/font12.c`
- `src/battery.c`
- `src/motor.c`
- `src/encoder.c`
- `src/motion.c`
- `src/ina219.c`

## 3. SPL 源文件（从 SPL V3.5.0 加入 Library Group）

- `stm32f10x_adc.c`
- `stm32f10x_dma.c`
- `stm32f10x_gpio.c`
- `stm32f10x_i2c.c`
- `stm32f10x_rcc.c`
- `stm32f10x_spi.c`
- `stm32f10x_tim.c`
- `stm32f10x_usart.c`
- `misc.c`

## 4. CMSIS/启动文件（按芯片容量选择 MD 系列）

- `startup_stm32f10x_md.s`
- `core_cm3.c`（若所用 Keil/ARMCC 模板要求）
- `system_stm32f10x.c`

## 5. 包含路径

- `thermal_robot\stm32\inc`
- SPL：`STM32F10x_StdPeriph_Driver\inc`
- CMSIS core：`Libraries\CMSIS\CM3\CoreSupport`
- CMSIS device：`Libraries\CMSIS\CM3\DeviceSupport\ST\STM32F10x`

## 6. 编译与链接设置

- Target：STM32F103CB（128KB Flash / 20KB RAM）
- Crystal：8.000MHz
- ARMCC：默认 C99；勾选 SPL 要求的 `USE_STDPERIPH_DRIVER`、`STM32F10X_MD`
- Linker：Keil 自动选择 `STM32F10x_MD` 散列文件（128KB Flash / 20KB RAM）
- Debug：ST-Link，SWD（PA13/PA14）。代码保留 SWD、关闭 JTAG，禁止改回 JTAG。

## 7. 上电验证顺序

1. 烧录后先运行 USART1 调试台（115200）：输入 `help`。
2. 输入 `lcdtest`：ILI9341 应显示彩条、ASCII 与中文降级提示。
3. 输入 `bat`：读取 PA4 ADC 分压电池电压。
4. 接好 S31 的 USART2 交叉线后，观察界面刷新；输入 `link` 查看最近 5 帧 JSON。
5. `mtest/etest/go/turn/scan` 当前应返回“本功能未启用（采购清单无驱动板）”；补件后将 `board_config.h` 的 `ENABLE_MOTION_DRIVE` 改为 `1`。

## 8. 当前字库状态

`font12.c` 当前包含 76 个非空 12×12 界面字模，每字 24 字节（逐行、高字节在前、每行低 4 位无效），Unicode/GB2312、位宽和非空数据已检查。未收录汉字按 `?` 降级；上屏方向与可读性仍需 `lcdtest` 实物确认，尚非完整 500 字库。

## 9. 启动文件里必须改的一个数值

`startup_stm32f10x_md.s` 默认 `Stack_Size EQU 0x00000400`（1KB）。本工程的栈上会放一个 `json_frame_t`（约 144 字节）加若干行缓冲与浮点运算临时量，1KB 偏紧，建议改成：

```asm
Stack_Size      EQU     0x00000800      ; 2KB
Heap_Size       EQU     0x00000200      ; 0x200，保持默认
```

说明：本工程代码里没有 `malloc/free`，RAM 全部用静态缓冲；但 `debug_console.c` 的 `printf/snprintf` 在 ARMCC 标准库 + 非 microlib 下可能仍会用到少量堆，因此 `Heap_Size` 建议保持默认 `0x200`，不要归零。若确认改用 microlib，可再省这 512 字节。
