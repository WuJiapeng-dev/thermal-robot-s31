# 热成像诊断机器人 · 交付目录说明

## 2026-11-19 二次补充

真实工程及本目录源码已加入：停走采集握手、76 个静态中文字模、DMA 双缓冲地址切换修复、开机序号/SNTP 时间戳、结构化 JSONL 与伪彩色 BMP 归档、Top-3 云端 JSON，以及 STM32 显示和运动控制修复。

- **S31**：ESP-IDF 真实构建成功，证据为 `build_logs/s31_build_07.log`。
- **STM32**：已在 Linux 云主机用 `arm-none-eabi-gcc 10.3.1` 完整编译并链接。默认电机关闭版与运动启用版均为 **0 error / 0 warning**；日志为 `build_logs/stm32_build_04_default.log` 和 `stm32_build_04_motion.log`。
- **默认 STM32 固件**：`firmware/STM32F103CBT6/thermal_robot_stm32.bin`，38,324 字节，保持 `ENABLE_MOTION_DRIVE=0`。运动版只验证编译，不交付为烧录固件。
- **边界**：尚未进行 STM32 烧录、外设或整机联调。温度标定、ArUco/单应矩阵和可见光图像上传仍未实现。


生成时间：2026-11-19

## 目录里有什么

| 文件 / 目录 | 内容 |
|---|---|
| **`交接说明_给下一个AI.md`** | **改代码前先读这份**：文件职责表、已修过的坑、接口契约、报错速查、禁止事项 |
| **`报告/`** | **独立报告目录**：开发报告、方案核查、工程说明和云端编译报告；报告不放入 `main/` |
| `报告/方案对照_功能实现核查.md` | 对照 V2 方案说明书核对代码完成度 |
| `提示词_01_ESP32-S31.md` | 你给的需求（S31 主控侧） |
| `提示词_02_STM32F103CBT6.md` | 你给的需求（STM32 显示与通信侧） |
| `源码_01_ESP32-S31.md` | S31 首次源码汇编快照，最新代码以散文件为准 |
| `源码_02_STM32F103CBT6.md` | STM32 首次源码汇编快照，最新代码以散文件为准 |
| `报告/开发报告_S31.md` | S31 编译命令与输出、引脚表和不确定项 |
| `报告/开发报告_STM32.md` | STM32 云端 ARM GCC 实测结果、历史预算与不确定项 |
| `报告/STM32云端编译报告.md` | 本次云端构建环境、命令、尺寸、哈希和实机验证边界 |
| `报告/STM32工程建立与上电验证.md` | GNU Arm / Keil 建工程清单及上电验证顺序 |
| `main/ESP32-S31/` | S31 散文件：根工程骨架 + `main/` 源码 |
| `main/STM32F103CBT6/` | STM32 散文件：`inc/`、`src/`、`Makefile`、`stm32f103cb.ld`，不放报告 |

**本目录源码在 `main/` 里**：`main/ESP32-S31/` 28 个文件；`main/STM32F103CBT6/` 34 个文件（17 个头、15 个源、Makefile 和链接脚本）。默认 STM32 固件另放在 `firmware/STM32F103CBT6/`。

已核对真实工程与交付源码；云端构建修复的 10 个源码/构建文件 SHA-256 比对一致。`PROJECT_SETUP.md` 已按要求移出 `main`，现为 `报告/STM32工程建立与上电验证.md`。

注：2026-11-19 15:52 到 16:00 期间，这个归档目录被从 `源码/` 改名为 `main/`，`main/ESP32-S31/main/` 这种两层 `main` 是改名后的自然结果，不是重复副本。

## 两件必须知道的事

1. **S31 要编译，别在 `桌面\热成像\` 下编。** ESP-IDF 6.1 的构建链不支持非 ASCII 路径（会 `cannot read spec file`、ccgen 用 GBK 解码 UTF-8 路径）。本目录里的 `main/` 只是**阅读用副本**；真正能编的工程在：

   ```
   C:\Users\yyn\Desktop\thermal_robot
   ```

   编译命令（在该终端已激活 IDF 环境的前提下）：

   ```powershell
   $env:IDF_PY_BUILD_JOBS = "3"
   $env:IDF_COMPONENT_MANAGER = "0"
   idf.py --preview -C "C:\Users\yyn\Desktop\thermal_robot" build
   ```

   已实测通过：`Project build complete`，0 error / 0 warning；`thermal_robot_s31.bin` **551,280 字节**（15:47 修复「NAN 进 JSON」后的大小）。

2. **STM32 已完成云端 GNU Arm 构建。** Windows 本机仍未安装 Keil/ARM GCC，但云主机已真实编译和链接两种配置。默认固件在 `firmware/STM32F103CBT6/`，构建方法见 `报告/STM32工程建立与上电验证.md`，实测详情见 `报告/STM32云端编译报告.md`；不要再把“本机未编译”误写成“工程从未编译”。

## 当前功能边界

- STM32 已有 76 个非空 12×12 中文字模，未收录汉字仍显示 `?`；字模方向和 LCD 实机效果待上屏确认。
- S31 的 `raw_to_celsius()` 返回 NAN，下发温度恒为 -273.0，STM32 显示 `--`。需要黑体或原厂 NUC 表才能填。
- ArUco/单应矩阵和可见光图片上传尚未实现；STM32 尚未烧录或整机联调。

## 目录里另有三份 html（不在本次交接范围内）

`ESP32-S31热成像项目功能报告.html`（15:51）、`ESP32-S31_GPIO接线速查卡.html`（15:58）、`ESP32-S31_接线关系网络图.html`（16:00）这三份不是本目录源码交接的产物，也**不参与**上面的 `diff` 核对。内容与项目相关（功能解析、引脚速查、接线网络），但它们的引脚数据是独立整理的，**以 `main/ESP32-S31/main/bsp_pinmap.h` 为准**；若三者互相冲突，改 html 而不是改代码。

## 改代码的人先看这个

`交接说明_给下一个AI.md` 是给「拿到报错要改代码」的场景写的，关键内容：

- **改代码只能改 `C:\Users\yyn\Desktop\thermal_robot`**，本目录下的 `main\` 是归档副本，改它不生效也不能编译。
- 第 4 节列了本轮真实踩过并修掉的 9 个坑，每条都注明「改回更常规的写法会怎样」——包括 SDMMC 头文件、`esp_codec_dev` 的 const、`copy_slot` 的自死锁、以及刚修的 NAN 进 JSON。
- 第 5 节是接口契约（JSON 字段、温度哨兵、引脚唯一来源、队列满时不阻塞）。
- 第 6 节是报错速查表，按「配置 / 编译 / 链接 / 运行」四阶段分开。
- 第 7 节是未验证清单，**这些不是待办，是不要当成已验证**。
