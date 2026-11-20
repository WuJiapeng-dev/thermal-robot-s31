# thermal-robot-s31

PCB 热诊断机器人的固件与上位显示工程：**ESP32-S31 负责热像采集与热点判定，STM32F103CBT6 负责界面显示与运动控制**。

两块板通过 UART 以 921600 波特率交换 JSON 帧。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 硬件组成

| 部件 | 型号 | 角色 |
|---|---|---|
| 主控 | ESP32-S31（ESP32-S31-Function-CoreBoard-1） | 热像读取、热点分析、语音播报、SD 存储、上行链路 |
| 显示/运动 | STM32F103CBT6 | 2.8" ILI9341 屏显、按键、蜂鸣器、电机与编码器（默认关闭） |
| 热像模块 | MAG160Core（160×120，USB 私有 Bulk 协议） | 红外图像源 |
| 音频 | 板载 ES8311 + NS4150B | 语音播报 |
| 摄像头 | OV2640（可选，DVP） | 可见光辅助 |

> 热像模块是**厂商私有协议**，不是 UVC。协议常量的来源与免责见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) 第三节。

## 目录结构

```
main/ESP32-S31/            ESP-IDF 工程（共 30 个文件：根 4 + main/ 27）
├── CMakeLists.txt         工程根 CMake
├── sdkconfig.defaults     16MB Flash / Octal PSRAM 200MHz
├── partitions.csv         nvs + phy_init + 8MB factory
└── main/
    ├── app_thermal.c      USB 私有协议、切帧、双缓冲、标定下载
    ├── app_hotspot.c      中位数 + raw 差值 + 4 邻域连通 + 连续帧确认
    ├── app_calib.c        /sdcard/calib.csv 区域查表
    ├── app_storage.c      SDIO 4bit FAT32
    ├── app_vision.c       OV2640 SCCB 探测
    ├── app_audio.c        ES8311 初始化与采样自检
    ├── app_tts.c          SYN6288 语音合成（UTF-8 → GB2312 子集）
    ├── app_link.c         UART2 JSON 下发
    ├── app_cloud.c        WiFi/HTTP 上传（默认关闭）
    ├── app_time.c         NTP 授时
    └── app_console.c      esp_console 自测命令台

main/STM32F103CBT6/        STM32 工程（SPL V3.5.0，不用 HAL）
├── Makefile               arm-none-eabi-gcc 构建
├── stm32f103cb.ld         链接脚本（64KB Flash / 20KB RAM）
├── inc/                   17 个头文件
└── src/                   15 个源文件

firmware/STM32F103CBT6/    编译产物（elf / hex / bin / map）
build_logs/               真实编译日志
报告/                     引脚表、功能核查等文档
```

## 构建

### ESP32-S31

目标 `esp32s31` 在 ESP-IDF v6.1 中是 **preview target**，`idf.py` 必须带 `--preview`。

```powershell
# 先激活 IDF 环境
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"

cd main/ESP32-S31
idf.py --preview build
```

**首次构建需要联网**：`main/idf_component.yml` 声明了 `espressif/usb 1.6.0` 与
`espressif/esp_codec_dev 1.6.2`，组件管理器会自动拉取到工程根的 `managed_components/`
（该目录**不在本仓库中**，`.gitignore` 也不会忽略它——第一次 build 后会变成未跟踪目录，
按需自行决定是否提交）。

构建慢可设 `$env:IDF_PY_BUILD_JOBS = "3"` 控制并行度。

若你已有这两份组件的本地副本，可放到 `main/ESP32-S31/managed_components/` 下，
再设 `$env:IDF_COMPONENT_MANAGER = "0"` 走离线构建——这是开发机上的做法，不是默认路径。

> **工程路径必须是纯 ASCII**，ESP-IDF 6.1 的构建链不认非 ASCII 路径，
> 否则会卡在 `cannot read spec file` 或 ccgen 的 GBK 解码错误。

烧录（`esp32s31` 目标支持见 `esptool 5.6.0` 的 `targets/esp32s31.py`）：

```powershell
idf.py --preview -p COM3 flash monitor
```

烧录参数已固化在 `build/flash_project_args` 生成逻辑中：`dio / 80MHz / 16MB`，
偏移 `0x2000` bootloader、`0x8000` 分区表、`0x10000` 应用。

### STM32F103CBT6

需要 `arm-none-eabi-gcc` 与 **STM32F10x SPL V3.5.0**（源码不随本仓库分发）：

```bash
cd main/STM32F103CBT6
make SPL_DIR=/path/to/STM32F10x_StdPeriph_Lib_V3.5.0
```

产物在 `build/`，烧录用 ST-Link（SWD）或串口 ISP。

## 已知限制

诚实说明当前完成度，避免误用：

| 项 | 状态 | 说明 |
|---|---|---|
| 温度换算 | ❌ 未实现 | `raw_to_celsius()` 返回 `NAN`，帧里温度字段固定填 `-273.0` 作哨兵。缺黑体标定或原厂 NUC 表，**不能用它读绝对温度** |
| 热点判据 | ⚠️ 待标定 | 用的是 raw counts 相对中位数的差值（`HOTSPOT_RAW_DELTA=1500`），这个 1500 是估计值，不是「8°C」的换算结果 |
| STM32 中文字模 | ❌ 空 | `font12.c` 有 72 字目录但 bitmap 全为 0，汉字降级显示 `?`，ASCII 正常 |
| 绝对定位 | ❌ 未实现 | 无 ArUco / 单应矩阵，只支持静态标定表查区域 |
| 实机验证 | ❌ 未做 | 编译通过，**从未烧录运行**。所有「枚举成功 / 挂载成功」均为未验证状态 |
| 运动模块 | ⏸ 预留 | `ENABLE_MOTION_DRIVE=0`，TB6612 + 编码器代码完整但默认不编译进逻辑 |
| WiFi 上传 | ⏸ 默认关闭 | `APP_WIFI_ENABLE=0`，空凭据不会发起连接 |

## 引脚

所有引脚集中在两个文件，改线只改这里：

- S31：`main/ESP32-S31/main/bsp_pinmap.h`
- STM32：`main/STM32F103CBT6/inc/board_config.h`

两块板的连线：

```
S31 GPIO46 (TX)  ──→  STM32 PA3 (USART2_RX)
S31 GPIO37 (RX)  ←──  STM32 PA2 (USART2_TX)
GND              ────  GND          ← 必须共地
```

## 参与协作

多人协作的接手步骤、分工边界、分支与提交约定、以及几个「看着像 bug 但别改」的位置，见
[`CONTRIBUTING.md`](CONTRIBUTING.md)。

简要约定：`main` 保持随时可编译，改动走 `feat/` `fix/` `docs/` 分支 + PR；串口 JSON 字段的
增删属于破坏性变更，必须两端同步改。

## 许可

本项目源码以 **MIT** 许可发布，全文见 [`LICENSE`](LICENSE)。

第三方组件（ESP-IDF、Espressif usb / esp_codec_dev、FreeRTOS、ST SPL）的归属与许可见
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。**注意 SPL 源码不在本仓库内**，
自行获取时需遵守 ST 的许可条款。

`README_交付说明.md` 是项目内部的交接记录，不属于开源文档的一部分，但一并保留以便追溯。
