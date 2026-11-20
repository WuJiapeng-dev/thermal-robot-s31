# STM32F103CBT6 云端编译与链接报告

日期：2026-11-19  
目标芯片：STM32F103CBT6（Cortex-M3，128KB Flash，20KB RAM）  
报告性质：真实 ARM GCC 编译、链接及产物核验记录，不是实机验收报告

## 1. 结论

本次已在 Linux 云主机中对 STM32 工程进行真实的全量编译和链接，不是仅做静态检查。

- 默认配置（`ENABLE_MOTION_DRIVE=0`）：**0 error / 0 warning**，成功生成 ELF、BIN、HEX 和 MAP。
- 运动功能启用配置（`ENABLE_MOTION_DRIVE=1`）：**0 error / 0 warning**，用于验证电机、编码器、INA219 和停走采集代码的启用分支。
- 默认固件已回传到交付目录；运动版只保留构建日志，不作为烧录固件交付。
- 云端修复和构建文件已同步回 Windows 真实工程及交付源码，10 个同步源码/构建文件的 SHA-256 比对无不一致。

**当前只证明 GNU Arm 编译和链接通过，不代表已经烧录，也不代表 LCD、串口 DMA、电机或整机功能已经实测通过。**

## 2. 构建环境与工程配置

| 项目 | 实际配置 |
|---|---|
| 构建环境 | Linux 云主机 |
| 编译器 | `arm-none-eabi-gcc 10.3.1 20210621` |
| 标准外设库 | STM32F10x Standard Peripheral Library V3.5.0 |
| 目标宏 | `USE_STDPERIPH_DRIVER`、`STM32F10X_MD` |
| 启动文件 | `startup_stm32f10x_md.s`（TrueSTUDIO/GCC 版本） |
| 链接脚本 | 新增 `stm32f103cb.ld`，128KB Flash、20KB RAM |
| 栈/堆预留 | 栈 `0x800`（2KB），堆 `0x200`（512B） |
| 编译选项 | C99、Cortex-M3、Thumb、`-Os`、`-Wall -Wextra`、段级垃圾回收 |
| 默认安全开关 | `ENABLE_MOTION_DRIVE=0` |

工程新增的 `Makefile` 会编译全部应用源文件、工程要求的 9 个 SPL 模块、CMSIS 系统文件和启动文件，再完成链接及 BIN/HEX 转换。

## 3. 本次实际修复

1. **补齐 SPL 断言配置**  
   首次构建因 `assert_param` 未定义，在 SPL 的 ADC/DMA 源文件中形成隐式函数声明而停止。已在 `inc/stm32f10x_conf.h` 增加 `USE_FULL_ASSERT` 分支；未启用完整断言时定义为空操作。

2. **清理条件编译警告**  
   默认关闭运动时，`encoder.c`、`ina219.c`、`motion.c` 中只供运动分支使用的静态变量和函数会产生未使用警告。现已用 `ENABLE_MOTION_DRIVE` 包住对应实现，不改变默认行为。

3. **清理调试台和电机禁用分支警告**  
   删除未使用的调试写字符串辅助函数，显式处理禁用分支参数；运动启用时出现的 `print_motion_disabled` 未使用警告也已通过条件编译处理。

4. **修正 UI 状态行缓冲长度**  
   将状态行缓冲扩大为 `UI_MAX_TEXT_CHARS + 12U`，消除 `snprintf` 潜在截断警告。

5. **允许构建参数覆盖运动开关**  
   `board_config.h` 中默认仍为 0，但可用 `EXTRA_CFLAGS=-DENABLE_MOTION_DRIVE=1` 构建独立运动验证版本，避免修改源码后误覆盖默认固件。

## 4. 实际构建命令

在包含 `stm32` 工程的目录中执行，SPL 默认位于 `../spl`，也可通过 `SPL_DIR` 指定：

```sh
# 默认配置：电机关闭
make -C stm32 clean
make -C stm32 -j3

# 运动启用配置：独立输出目录，不覆盖默认固件
make -C stm32 -j3 BUILD_DIR=build_motion EXTRA_CFLAGS=-DENABLE_MOTION_DRIVE=1
```

最终交付证据对应第四次干净重建日志：

- `build_logs/stm32_build_04_default.log`
- `build_logs/stm32_build_04_motion.log`

## 5. 编译和链接结果

| 配置 | text / B | data / B | bss / B | BIN / B | 错误 | 警告 |
|---|---:|---:|---:|---:|---:|---:|
| 默认，电机关闭 | 37,820 | 500 | 7,052 | 38,324 | 0 | 0 |
| 运动功能启用 | 42,324 | 500 | 7,100 | 42,828 | 0 | 0 |

说明：

- 表中 `text/data/bss` 来自 `arm-none-eabi-size` 的真实输出，不是此前按源码估算的预算。
- BIN 大小与 `text + data` 不必逐字节相等，可能包含段对齐及可加载数据。
- 默认 BIN 约占 128KB Flash 的 29.2%，运动验证 BIN 约占 32.7%。
- 默认固件保持电机关闭；运动配置的数字只用于证明该分支可编译，不建议在未完成硬件检查和机械标定前烧录运动版。

## 6. 固件完整性与入口检查

默认 BIN：

```text
文件：thermal_robot_stm32.bin
大小：38,324 字节
SHA-256：aff43864fec650792abef296c5c9b47b7e6dda8ae8beca4ea173665b6b0762a7
```

默认 ELF 的向量表和入口已检查：

| 检查项 | 结果 |
|---|---|
| 向量表地址 | `0x08000000` |
| 初始栈顶 | `0x20005000` |
| 复位向量 | `0x08007F61`，Thumb 位有效 |
| `main` | 已在 ELF 中解析并链接 |
| RAM 末端 | `_estack = 0x20005000` |

## 7. 交付目录位置

以下路径均相对于交付根目录 `S31-main1`：

| 内容 | 路径 |
|---|---|
| 默认 BIN | `firmware/STM32F103CBT6/thermal_robot_stm32.bin` |
| 默认 HEX | `firmware/STM32F103CBT6/thermal_robot_stm32.hex` |
| 调试用 ELF | `firmware/STM32F103CBT6/thermal_robot_stm32.elf` |
| 链接映射 | `firmware/STM32F103CBT6/thermal_robot_stm32.map` |
| 默认构建日志 | `build_logs/stm32_build_04_default.log` |
| 运动配置构建日志 | `build_logs/stm32_build_04_motion.log` |
| STM32 当前源码 | `main/STM32F103CBT6/` |
| 工程建立与上电说明 | `报告/STM32工程建立与上电验证.md` |
| STM32 综合开发报告 | `报告/开发报告_STM32.md` |
| 本报告 | `报告/STM32云端编译报告.md` |

## 8. 尚未验证的事项

1. 未使用 Keil MDK/ARMCC 再编译；当前结论只对应 GNU Arm 工具链。
2. 未烧录到 STM32F103CBT6，未检查实际启动和串口日志。
3. 未实测 ILI9341 的彩条、方向、中文点阵可读性及 SPI 稳定性。
4. 未实测 USART2 921600、DMA 双缓冲和 S31 双向 `capture/capture_done` 联调。
5. 未实测电池分压、电量百分比和蜂鸣器。
6. 未进行电机、编码器、INA219、PI 控制或停走采集运动测试；机械常量仍需实测标定。
7. 本次构建不补齐 S31 侧温度标定、ArUco/单应矩阵和可见光图片上传等功能缺口。

## 9. 建议的实机验证顺序

1. 只烧录默认电机关闭版，确认供电和 SWD 连接正常。
2. 打开 USART1 调试台（115200），输入 `help`，确认程序正常启动。
3. 执行 `lcdtest`，检查彩条、ASCII、中文字模方向和缺字降级。
4. 执行 `bat`，用万用表核对电池电压。
5. 接好 S31 与 STM32 的交叉串口及共地，使用 `link` 检查最近 JSON 帧和丢帧计数。
6. 核对 `capture/capture_done` 的序号及实际采集帧数。
7. 只有在驱动板、电机、编码器和 INA219 接好，并完成机械常量标定后，才单独构建和测试运动版。
