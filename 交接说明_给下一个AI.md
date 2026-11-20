# 交接说明 · 给下一个接手改代码的 AI

## 2026-11-19 二次补充索引

- 真实工程仍改 `C:\Users\yyn\Desktop\thermal_robot`；本次 23 个修改文件已同步到 `S31-main1\main` 并做 SHA-256 一致性校验。
- S31 新增 `app_time.c/h`；最终构建证据为 `S31-main1\build_logs\s31_build_07.log`。
- 新增双向 `capture/capture_done` 协议；S31 热点结构新增 `delta_c`，STM32 新增采集序号/实际帧数核对。
- STM32 字库已有 76 个非空字模；`comm.c` 的 DMA 乒乓重载必须同时写 `CMAR` 和 `CNDTR`，禁止退回只改计数器的版本。
- `源码_01/02_*.md` 是首次交付汇编；后续修改以 `main/ESP32-S31` 与 `main/STM32F103CBT6` 散文件为准。
- STM32 已在云主机用 ARM GCC 10.3.1 完整编译/链接：默认和运动启用两种配置均 0 error / 0 warning；证据为 `build_logs/stm32_build_04_default.log`、`stm32_build_04_motion.log`。默认 BIN 位于 `firmware/STM32F103CBT6/`，仍关闭电机。
- STM32 新增 GNU `Makefile` 与 `stm32f103cb.ld`；`assert_param` 配置及警告清理已同步真实工程和交付散文件。编译通过不等于烧录或实机联调通过。
- 温度标定、ArUco/单应矩阵和可见光图片上传仍缺实物数据，不得用虚构常量或占位结果冒充完成。


生成时间：2026-11-19 15:50
适用对象：拿到编译/运行报错、需要修改本工程代码的 AI 或工程师。
本文只写**可执行的事实**和**踩过的坑**，不写需求复述。需求原文见同目录 `提示词_01` / `提示词_02`。

---

## 0. 开始之前：三条不可搞错的事

### 0.1 改代码只能改 `thermal_robot`，不能改 `热成像\main` 下的归档副本

| 路径 | 性质 | 能否编译 |
|---|---|---|
| **`C:\Users\yyn\Desktop\thermal_robot`** | **唯一的真实工程**，`idf.py build` 在这里跑通 | ✅ 能 |
| `C:\Users\yyn\Desktop\热成像\main\ESP32-S31\` | 归档副本（只读用） | ❌ 不能 |
| `C:\Users\yyn\Desktop\热成像\main\STM32F103CBT6\` | 归档副本（只读用） | ❌ 不能 |
| `C:\Users\yyn\Desktop\热成像\源码_01/02_*.md` | 源码汇编文本（阅读用） | ❌ 不能 |

**在 `热成像\` 下改任何东西都不会生效**，原因有两条：一是那只是副本，二是 ESP-IDF 6.1 的构建链不支持非 ASCII 路径（会报 `cannot read spec file`、ccgen 用 GBK 解码 UTF-8 路径失败）。

改完代码如果想让归档副本同步，需要手动重跑一次复制（见第 9 节），否则归档会过期。

### 0.2 两侧的编译状态不一样

| 侧 | 状态 | 证据 |
|---|---|---|
| ESP32-S31 | **Windows ESP-IDF 编译通过** | `build_logs\s31_build_07.log` |
| STM32F103CBT6 | **云主机 ARM GCC 编译/链接通过，默认与运动配置均 0 error / 0 warning** | `build_logs\stm32_build_04_default.log`、`stm32_build_04_motion.log` |

STM32 默认 BIN 在 `firmware\STM32F103CBT6\`，`ENABLE_MOTION_DRIVE=0`。运动版只用于编译验证，不要误烧。两侧的“编译通过”都不等于外设和整机联调通过。

### 0.3 事实优先级

本文 > `报告/开发报告_S31.md` / `报告/开发报告_STM32.md` > 代码注释 > 通用经验。

代码注释里凡是写「待标定」「待实测」「未验证」的，都不是笔误，是**有意保留的不确定项**，不要为了"看起来完整"把它们改成确定值。

---

## 1. 三十秒定位：看到这个报错，先开哪个文件

| 报错/现象里出现的关键词 | 先打开 |
|---|---|
| `app_main`、初始化顺序、INIT 行、扫描主循环 | `main\main.c` (170 行) |
| 引脚、GPIO 号、重复占用、strapping | `main\bsp_pinmap.h` (66 行) |
| 阈值、超时、波特率、帧长、任务栈、队列长度 | `main\app_config.h` (124 行) |
| `usb_host`、`bulk`、`0x81/0x82/0x83`、切帧、`magic`、帧率、掉线重连 | `main\app_thermal.c` (904 行) |
| 中位数、flood fill、热点、delta、`conf` | `main\app_hotspot.c` (256 行) |
| `calib.csv`、区域名、坐标命中 | `main\app_calib.c` (170 行) |
| SD 挂载、`sdmmc`、FAT、`/sdcard/frames`、写文件失败 | `main\app_storage.c` (224 行) |
| SCCB、OV2640、XCLK、PID/VER | `main\app_vision.c` (148 行) |
| ES8311、I2S、`esp_codec_dev`、采样、增益 | `main\app_audio.c` (295 行) |
| SYN6288、GB2312、播报缺字、TTS 队列 | `main\app_tts.c` (267 行) |
| USART2、JSON 字段、`hot/hb/st`、STM32 收不到 | `main\app_link.c` (244 行) |
| WiFi、HTTP、上传 404/超时 | `main\app_cloud.c` (219 行) |
| `thermal_test`、`usb_probe` 等命令不响应 | `main\app_console.c` (183 行) |
| `Program Size`、RAM/Flash 超、`Heap_Size` | `stm32\PROJECT_SETUP.md` + 启动文件 |
| LCD 花屏、偏色、方向、中文显示 `?` | `stm32\src\ili9341.c` (297 行) + `font12.c` (85 行) |
| STM32 收到乱码、丢帧、`oversize` | `stm32\src\comm.c` (181 行) |
| STM32 JSON 解析失败、字段缺失 | `stm32\src\json_frame.c` (267 行) |
| 界面不刷新、温度显示 `--`、断链告警 | `stm32\src\ui.c` (157 行) + `app.c` (104 行) |
| 电机/编码器/INA219 相关 | `motor.c` / `encoder.c` / `motion.c` / `ina219.c`（默认全部关闭） |

---

## 2. 文件职责表

### 2.1 ESP32-S31（`thermal_robot\main`，11 个 .c + 12 个 .h，3,508 行）

| 文件 | 行数 | 职责 | 关键函数（行号） | 改它会影响 |
|---|---|---|---|---|
| `main.c` | 170 | 上电初始化链、扫描主循环 | `app_main`(114)、`run_scan`(47)、`wait_for_thermal`(38) | 全部模块的启动顺序 |
| `app_config.h` | 124 | 全部阈值/协议常量/任务栈 | — | 几乎全部模块，改前先 `grep` 谁在用 |
| `bsp_pinmap.h` | 66 | 全部引脚宏 | — | 所有驱动，改引脚只改这里 |
| `app_thermal.c` | 904 | USB Host 私有协议、切帧、双缓冲、标定下载 | `open_thermal_device`(406)、`command_transaction`(248)、`parse_image_bytes`(147)、`deliver_frame`(119)、`thermal_get_frame`(676)、`raw_to_celsius`(703) | 热点、链路、主循环全部数据源 |
| `app_hotspot.c` | 256 | 中位数基准 + 阈值 + 4 邻域聚类 + 连续帧确认 | `app_hotspot_analyze`(151)、`image_median`(73)、`select_kth`(37) | 误报/漏报率 |
| `app_calib.c` | 170 | `calib.csv` 六字段解析与查表 | `app_calib_init`(79)、`app_calib_find`(138)、`parse_csv_line`(37) | 区域名/坐标显示 |
| `app_storage.c` | 224 | SDIO 4bit FAT32、目录、日志、raw CSV 快照 | `app_storage_init`(33)、`app_storage_self_test`(100)、`app_storage_write_thermal_snapshot`(169) | 标定表读取、快照 |
| `app_vision.c` | 148 | OV2640：LEDC XCLK + SCCB 读 PID/VER | `app_vision_init`(55)、`app_vision_probe`(95) | 仅自检，无取图 |
| `app_audio.c` | 295 | 板载 ES8311：I2C + I2S + 1 秒采样自检 | `app_audio_init`(128)、`app_audio_self_test`(234) | 仅自检，本轮无语音识别 |
| `app_tts.c` | 267 | SYN6288 UART1 + 队列 + UTF-8→GB2312 子集 | `app_tts_speak`(232)、`utf8_to_gb2312`(98)、`lookup_gb2312`(86)、`send_gb2312`(127) | 播报完整性（受字表限制） |
| `app_link.c` | 244 | USART2 921600，手写 JSON，发送队列 | `app_link_send_hotspot`(147)、`app_link_send_heartbeat`(184)、`app_link_set_stage`(197) | STM32 显示的一切 |
| `app_cloud.c` | 219 | WiFi/HTTP 上传（默认关闭） | `app_cloud_init`(42)、`post_buffer`(133) | 无（`APP_WIFI_ENABLE=0` 时是空壳） |
| `app_console.c` | 183 | esp_console 9 条自测命令 | `app_console_init`(142) | 现场排障手段 |

### 2.2 STM32F103CBT6（`thermal_robot\stm32`，15 个 .c + 17 个 .h，2,567 行）

| 文件 | 行数 | 职责 | 关键函数（行号） |
|---|---|---|---|
| `src/app.c` | 104 | 主循环：解析 JSON → 刷 UI → 断链/电量/蜂鸣 | `app_init`(18)、`app_link_poll`(35)、`app_periodic_poll`(75)、`main`(96) |
| `src/comm.c` | 181 | USART2 DMA + IDLE，双 512B 乒乓，帧取用/归还 | `comm_init`(23)、`comm_usart2_irq`(86)、`comm_acquire_frame`(137)、`comm_release_frame`(144) |
| `src/json_frame.c` | 267 | 手写 JSON 解析（无 cJSON），三种帧 + 缺失位掩码 | `json_parse_frame`(195)、`parse_number`(113)、`parse_string`(73) |
| `src/ui.c` | 157 | 四区块界面渲染、断链提示、电量 | `ui_show_hot`(106)、`ui_show_stage`(123)、`ui_set_link`(130)、`format_temperature`(25) |
| `src/ili9341.c` | 297 | SPI1 18MHz、初始化序列、矩形/ASCII/中文绘制 | `lcd_init`(106)、`lcd_draw_string`(262)、`lcd_draw_chinese`(238) |
| `src/font12.c` | 以当前文件为准 | 76 个非空 12×12 中文字模及 Unicode/GB2312 目录 | `font12_get`、`font12_has_verified_glyphs` |
| `src/board.c` | 66 | 时钟/GPIO/蜂鸣器/心跳灯 | `board_init`(11)、`board_background_task`(42) |
| `src/timebase.c` | 33 | SysTick 毫秒时基 | `timebase_millis`(17)、`timebase_expired`(30) |
| `src/stm32f10x_it.c` | 56 | 中断向量：SysTick、USART2、HardFault | `USART2_IRQHandler`(53)、`HardFault_Handler`(12) |
| `src/debug_console.c` | 193 | USART1 115200 命令台 + 5 帧 JSON 历史 | `handle_command`(106)、`link_history_push`(42) |
| `src/battery.c` | 95 | PA4/ADC1_IN4，100k/33k 分压，中位数 | `battery_read_millivolts`(72)、`battery_percent`(80) |
| `src/motor.c` | 153 | TB6612 + TIM1 PWM（默认关闭） | `motor_init`(42)、`motor_set`(90) |
| `src/encoder.c` | 137 | TIM2/TIM4 正交解码（默认关闭） | `encoder_init`(50)、`encoder_update`(72) |
| `src/motion.c` | 178 | 梯形加减速 + PI 速度环（默认关闭） | `motion_move_mm`(122)、`motion_turn_deg`(138) |
| `src/ina219.c` | 143 | I2C1 PB8/PB9 电流监测（默认关闭） | `ina219_read`(118) |
| `inc/board_config.h` | 176 | STM32 全部引脚/宏/常量（唯一来源） | — |

---

## 3. 构建与验证

### 3.1 S31 编译（唯一可用路径）

必须在**已激活 IDF 环境**的 PowerShell 里执行。普通 PowerShell / Git Bash / cmd 都会失败，本机执行策略是 Restricted，`.ps1` 无法直接加载，所以要先进程级 Bypass。

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
$env:IDF_PY_BUILD_JOBS = "3"
$env:IDF_COMPONENT_MANAGER = "0"
idf.py --preview -C "C:\Users\yyn\Desktop\thermal_robot" build
```

四条必须有，缺一条就出错：

| 项 | 为什么必须有 | 缺了会怎样 |
|---|---|---|
| 激活 profile | 设 `IDF_PATH`、Python venv、工具链 PATH | `idf.py` 找不到 / 用了错误的 Python |
| `IDF_PY_BUILD_JOBS=3` | 本机内存限制；`-j` 参数在这个 IDF 版本上非法 | 并行编译 OOM 或参数报错 |
| `IDF_COMPONENT_MANAGER=0` | 工程用本地 `managed_components`，不联网 | 卡在 `Cannot establish a connection to the component registry` |
| `--preview` | `esp32s31` 是 preview target | `ERROR: Unsupported target` 类报错 |

**成功标志**（必须同时出现）：

```
Project build complete. To flash, run:
...
EXITCODE=0
```

产物与当前基线（改动前先记下，改完对比有没有暴涨）：

| 文件 | 基线大小 |
|---|---|
| `build\thermal_robot_s31.bin` | **551,280 B**（`0x86970`，含 4.8 的 NAN 修复；修复前为 551,200 B） |
| `build\bootloader\bootloader.bin` | 23,168 B |
| `build\partition_table\partition-table.bin` | 3,072 B |

app 分区 8 MB，剩余 `0x779690`（93%）。

`idf.py --preview size` 基线（NAN 修复前）：Flash 471,512 B / 8 MB，DIRAM 92,554 / 503,744（18.37%）。

日志编码：`build_logs\*.log` 由 `Tee-Object` 写出，是 **UTF-16LE**。用 `grep` 前先 `iconv -f UTF-16LE -t UTF-8`，否则匹配不到任何内容——这是排查时最容易浪费时间的点。

### 3.2 S31 自测命令（烧录后才有意义，本轮未烧录）

串口 115200，`help` 看全表。9 条：`thermal_test` `usb_probe` `hotspot_test` `sd_test` `calib_test` `tts_test` `link_test` `cam_test` `audio_test`。

设计约定：**任何一条在没有硬件时都必须打印明确的失败原因并返回非 0，不能卡死或重启**。如果发现某条命令会挂住，那是 bug，优先修。

### 3.3 STM32 编译

已在云主机用 GNU Arm 完成真实编译/链接，默认和运动配置均为 0 error / 0 warning。完整命令、尺寸和哈希见 `报告/STM32云端编译报告.md`，工程说明见 `报告/STM32工程建立与上电验证.md`。

- 默认：`text/data/bss = 37820/500/7052`，BIN 38,324 B，`ENABLE_MOTION_DRIVE=0`。
- 运动：`text/data/bss = 42324/500/7100`，只作编译验证，不交付运动版烧录 BIN。
- Keil/ARMCC、烧录和实机功能仍未验证。

---

## 4. 已经修过的坑 —— 不要"优化"回去

以下 9 条都是本轮真实踩到并修掉的。每一条都写了「如果改回更常规的写法会怎样」，因为下一个 AI 很容易出于"代码规范"直觉把它改坏。

### 4.1 `main/CMakeLists.txt` 里不能有 `json` 组件

- **现象**：`Failed to resolve component 'json' required by component 'main': unknown name`
- **根因**：ESP-IDF v6.1 **没有**名为 `json` 的组件，cJSON 也不是以组件名 `json` 暴露的。
- **现在怎么写的**：不依赖任何 JSON 库；`app_link.c` 用手写 `snprintf` 拼接（定长缓冲 + 长度检查），STM32 侧 `json_frame.c` 用手写递归下降解析。
- **改回去会怎样**：配置期直接失败。要加库请显式写 `espressif/xxx` 并确认它支持 `esp32s31`。

### 4.2 `esp_vfs_fat.h` 不再顺带包含 SDMMC 头

- **现象**：`SDMMC_HOST_DEFAULT`、`sdmmc_slot_config_t`、`SDMMC_SLOT_NO_CD` 全部「未声明」
- **根因**：v6.1 把这些搬到了独立组件，`esp_vfs_fat.h` 只前置声明。
- **现在怎么写的**：`app_storage.c` 显式 include
  ```c
  #include "driver/sdmmc_host.h"
  #include "sd_protocol_defs.h"
  ```
- **改回去会怎样**：编译期未声明。**不要**通过"删掉这些结构体用法、改成旧的 `sdmmc_host_init()` 流程"来绕过，新 API 才是 v6.1 的正确形态。

### 4.3 `SDMMC_OCR_SDHC` 已改名

- **现象**：宏未定义
- **现在怎么写的**：`SD_OCR_SDHC_CAP`（来自 `sd_protocol_defs.h`）
- **注意**：这是仅用于日志打印卡类型的判断，改错不影响功能，但会让日志误导。

### 4.4 `esp_codec_dev` 的配置结构体不能加 `const`

- **现象**：`passing argument N of 'xxx' discards 'const' qualifier`，且 v6.1 把它从 warning **升级为 error**（因为工程开了 `-Werror`）
- **影响 4 处**：`audio_codec_new_i2s_data`、`audio_codec_new_i2c_ctrl`、`esp_codec_dev_new`、`esp_codec_dev_open`
- **现在怎么写的**：`i2s_cfg`、`i2c_cfg`、`device_cfg`、`sample_info` 四个局部结构体**不加 const**（语义完全不变，只是形参不是 const）
- **改回去会怎样**：`app_audio.c` 直接编不过。如果看到这 4 处没有 const 觉得别扭，那是**故意的**。

### 4.5 `esp_console_init` 需要传配置

- **现象**：`too few arguments to function 'esp_console_init'`
- **现在怎么写的**：传 `esp_console_config_t{ max_cmdline_length = 128, max_cmdline_args = 8 }`
- **改回去会怎样**：编不过。

### 4.6 `app_thermal.c` 的组装缓冲必须与交付缓冲分离

- **历史问题**：早先两个 38,456 B 缓冲既用于组装流式数据、又用于对外交付，导致正在被 `thermal_get_frame()` 拷贝的帧被下一帧覆盖，出现撕裂/花屏。
- **现在怎么写的**：三块 PSRAM 缓冲
  - `s_assembly_buffer`（第 33 行声明，577 行分配）——只用于流式拼帧
  - `s_frame_buffers[2]`（578–579 行）——只用于交付
  - 拼满一帧后 `deliver_frame()` 整块 `memcpy` 到 slot
- **改回去会怎样**：高帧率下出现偶发画面撕裂，且极难复现、极难定位（因为要恰好在 memcpy 时来新数据）。**不要为了省 38 KB PSRAM 把组装缓冲合并回去**，16 MB PSRAM 不缺这点。

### 4.7 `copy_slot()` 内部不能获取互斥锁

- **现在怎么写的**（`app_thermal.c:662`）：
  - `copy_slot()` **只做拷贝，不加锁**
  - 两个调用方 `thermal_get_frame()`(676) 和 `app_thermal_get_latest()`(691) 各自在外层 `xSemaphoreTake(s_frame_mutex, ...)`
- **改回去会怎样**：`app_thermal_get_latest()` 先拿锁再调 `copy_slot()`，而 `copy_slot()` 再拿同一把非递归锁 → **自死锁，整机卡住**。
- **判断依据**：FreeRTOS 的 `xSemaphoreCreateMutex()` 创建的是非递归互斥量，同一任务重复获取必然死锁。

### 4.8 NAN 不能直接进 JSON（本轮新修，2026-11-19 15:45）

- **现象**：`hotspot->temp_c` 来自 `raw_to_celsius()`，固定返回 `NAN`；`snprintf("%.1f", NAN)` 在 newlib 里输出字符串 `nan`，拼出来是 `"t":nan` —— **不是合法 JSON**。
- **影响**：STM32 侧 `parse_number()` 解析失败 → `missing_mask` 标 `t` 缺失 → 温度列降级显示。虽然不会崩，但属于真实协议违规，且会掩盖后续真正的解析问题。
- **现在怎么写的**（`app_link.c:166`）：
  ```c
  const double temp_out = (hotspot->temp_c > APP_TEMP_INVALID_C + 1.0f)
                              ? (double)hotspot->temp_c
                              : (double)APP_TEMP_INVALID_C;
  ```
  利用「任何与 NAN 的比较都为 false」这一点，NAN 自动落到哨兵 `-273.0`。
- **同时改的**：`main.c` 的热点日志把 `%.1f` 换成先格式化到 `temp_text[]`，无效时打印 `--`，避免日志里出现 `temp=nan` 误导排查。
- **改回去会怎样**：回到协议违规状态。**以后任何新增的浮点字段都要先判 `isnan` 或用比较法兜底**，不要直接塞进 `snprintf`。

### 4.9 TB6612 的 brake / coast 不能用 sign 推导（STM32，2026-11-19 15:58 修）

- **现象**：`motor_brake()` 执行后车体全速前冲；`motor_coast()` 执行后立刻急停。
- **根因**：原实现用同一个 `sign` 反推两根方向脚
  ```c
  AIN1 = (sign >= 0) ? 1 : 0;
  AIN2 = (sign <= 0) ? 1 : 0;
  ```
  代入方案真值表：`sign=0` → `(1,1)` 是**短路制动**，`sign=+1` → `(1,0)` 是**正转**。于是 `brake(1)` 变成正转（还配了满占空比 `MOTOR_PWM_ARR`），`coast(0)` 变成制动，两个语义互换。`motion.c` 里有 4 处 `motor_brake()`（到位、欠压、超时、`motion_stop`），一旦打开 `ENABLE_MOTION_DRIVE`，第一次定位完成就冲出去。
- **现在怎么写的**：显式枚举 `MOTOR_DIR_FORWARD / REVERSE / BRAKE / COAST`，按方案第五章第五节真值表逐脚写死；`brake_both()` 统一做短路制动并把占空比归零；辅助函数全部移入 `#if ENABLE_MOTION_DRIVE`，开关为 0 时不产生未引用警告。
- **改回去会怎样**：**上电即危险**。任何"用正负号同时表达方向和停止"的简化写法都会重现这个 bug，因为 TB6612 的停止有两种（制动 1,1 与滑行 0,0），一个符号位表达不了三态。注意这是 STM32 侧，**未经编译验证**（本机无工具链）。

### 4.10 对照方案说明书时注意的两处"方案写错"

- 方案第二章说 MAG160Core 「经 USB 2.0 HS OTG 读取 UVC 视频流」——**错的**，它是厂商私有 bulk 协议（VID `0x833C`）。按 UVC 写会完全跑不通，代码按真实协议实现。
- 方案 BOM 是 OV5640（XCLK 24MHz），提示词与代码是 OV2640（XCLK 20MHz）。动摄像头代码前先确认实物是哪颗。


---

## 5. 接口契约（改一处会连锁坏）

### 5.1 JSON 帧格式（S31 → STM32，USART2 921600 8N1）

三种帧，每帧一行，UTF-8 单行 JSON，**单帧上限 512 B**（两侧宏必须同时改）。

| 类型 | 实际格式（`app_link.c` 为准确来源） |
|---|---|
| 热点 | `{"type":"hot","id":<raw_delta>,"grp":"<group_id>","zh":"<中文名>","x":<u16>,"y":<u16>,"t":<float1>,"dt":<float1>,"px":<u8>,"conf":<float2>}` |
| 心跳 | `{"type":"hb","ts":<ms>,"n":<热点数>}` 每秒 1 次 |
| 阶段 | `{"type":"st","stage":"idle\|scan\|analyze\|done","prog":<0-100>}` |

STM32 → S31：`{"cmd":"ready"}`、`{"cmd":"ack","id":<n>}`（目前 S31 侧未强依赖，属于预留）。

**改字段必须三处同步**：`app_link.c` 的 `snprintf` 格式串、`stm32\inc\json_frame.h` 的结构体与掩码、`stm32\src\json_frame.c` 的 `json_parse_frame()`。少改一处的后果是静默字段丢失（不会报错，只是 UI 显示原值）。

### 5.2 `thermal_frame_t` 的 `pixels` 由调用方提供

```c
typedef struct {
    uint16_t *pixels;   /* 调用方必须自己分配 THERMAL_PIXEL_COUNT 个 uint16 */
    ...
} thermal_frame_t;
```

`thermal_get_frame()` 和 `app_thermal_get_latest()` 都只 `memcpy` 到 `frame->pixels`，**不分配内存**。传 `pixels == NULL` 会返回 `ESP_ERR_INVALID_ARG`。主循环里那块 38,400 B 的扫描缓冲在 `main.c:152` 一次性申请，只在退出时释放。

### 5.3 温度哨兵 `-273.0`

| 位置 | 约定 |
|---|---|
| `app_config.h` | `APP_TEMP_INVALID_C (-273.0f)` |
| `bsp` / `ui.c` | `TEMPERATURE_INVALID (-273.0f)` → 显示 `--` |
| 链路 | 只要 `raw_to_celsius()` 还是返回 NAN，`t` 和 `dt` 恒为 -273.0 |

**不要**把这个哨兵改成 0 或删除。0 ℃ 是合法温度，用它做无效标记会导致「0 度」和「没数据」无法区分。

### 5.4 引脚只改 `bsp_pinmap.h` / `board_config.h`

S31 侧所有引脚在 `bsp_pinmap.h`，STM32 侧在 `inc/board_config.h`。**不要在任何驱动 .c 里写死 GPIO 号**。

已知冲突（重要）：

| 宏 | 值 | 冲突对象 |
|---|---|---|
| `PINMAP_EXT_MIC_BCLK` | GPIO35 | OV2640 D6 |
| `PINMAP_EXT_MIC_WS` | GPIO36 | OV2640 D7 |
| `PINMAP_EXT_MIC_DIN` | GPIO37 | 链路 USART2 RX |

这三个宏只在 `AUDIO_USE_EXTERNAL_I2S_MIC=1` 时生效，当前是 0。**将来要接外购 INMP441，必须先把这三个宏改到空脚**，否则摄像头和串口链路会同时静默失效——不会报编译错误。

### 5.5 队列满时丢数据，不阻塞

`app_link.c` 的发送队列长度 16（`LINK_QUEUE_LENGTH`），`app_tts.c` 的播报队列长度 4（`TTS_QUEUE_LENGTH`）。满时返回错误并记日志，**不阻塞调用方**（因为调用方是主循环，阻塞会拖垮采集）。

如果发现「热点消息丢失」，先看是不是队列满，**不要**改成 `portMAX_DELAY` 等待——那会把主循环拖死。

---

## 6. 报错速查

### 6.1 配置阶段

| 报错 | 原因 | 改哪里 |
|---|---|---|
| `Cannot establish a connection to the component registry` | 组件管理器试图联网 | 确认 `IDF_COMPONENT_MANAGER=0`；`managed_components\` 下两个组件目录存在且完整 |
| `Failed to resolve component 'xxx'` | 组件名不存在或没本地化 | 查 `main/idf_component.yml`；v6.1 无 `json` 组件 |
| `Directory 'build' doesn't seem to be a CMake build directory` | build 目录是中断构建留下的半成品 | 删掉整个 `build\` 重新 `idf.py --preview set-target esp32s31` |
| `Unsupported target` / target 相关 | 漏了 `--preview` | 所有 `idf.py` 命令补 `--preview` |

### 6.2 S31 编译阶段

| 报错 | 原因 | 改哪里 |
|---|---|---|
| `discards 'const' qualifier` | `-Werror` + esp_codec_dev 形参非 const | 见 4.4，去掉那 4 个结构体上的 `const` |
| `X undeclared` / `implicit declaration` | v6.1 拆分了组件头 | 先查该符号属于哪个组件，在 `main/CMakeLists.txt` 的 `REQUIRES` 里补组件名 |
| `too few/many arguments to function` | v6.1 API 签名变化 | 查 `D:\esp\v6.1\esp-idf\components\<组件>\include\` 下的真实声明 |
| `undefined reference to 'xxx'` | 源文件没加进 `SRCS` | `main/CMakeLists.txt` |
| `region 'xxx' overflowed` | 缓冲/栈超限 | 见 6.3 |

### 6.3 S31 运行阶段（需要烧录后才可能遇到）

| 现象 | 首查 | 说明 |
|---|---|---|
| `INIT THERMAL` 打印 FAIL | `app_thermal.c:593` `app_thermal_init` | 看前一行 USB Host install 返回值 |
| `usb_probe` 看不到端点 | `app_thermal.c:817` | 确认 VID `0x833C` / PID `0x0001` / `0x0002` 是否与实物一致 |
| 帧率低 / 丢弃帧 >1% | `app_config.h` 的 `THERMAL_USB_CHUNK_BYTES` | 从 4096 降到 1024 复测 |
| USB 反复重连 | `THERMAL_MAX_CONSECUTIVE_FAILS=5` 触发 | 提高该值只会掩盖问题，应先看 ACK 是否每次都读到 |
| `INIT SD` FAIL | `app_storage.c:33` | 4bit 模式对排线敏感，可临时改 `SD_BUS_WIDTH=1` 验证 |
| 播报缺字 | `app_tts.c:86` `lookup_gb2312` | 字表只有约 70 字，缺的字记日志后跳过，属已知限制 |
| 内存不足 / `PSRAM allocation failed` | `main.c:152`、`app_thermal.c:577` | 16 MB PSRAM，除非反复泄漏；用 `heap_caps_get_free_size` 打印核对 |
| 偶发画面撕裂 | 见 4.6 | 确认三块缓冲仍在，没有被合并 |

### 6.4 STM32 GNU Arm / Keil 编译

GNU Arm 已跑通，不再是“第一次被编译器看到”。在 SPL V3.5.0 位于 `../spl` 的云主机中：

```sh
make -C stm32 clean
make -C stm32 -j3
make -C stm32 -j3 BUILD_DIR=build_motion EXTRA_CFLAGS=-DENABLE_MOTION_DRIVE=1
```

- 两种配置均已 0 error / 0 warning；完整命令和尺寸见两份 `stm32_build_04_*.log`。
- 若 SPL 报 `assert_param` 隐式声明，检查 `inc/stm32f10x_conf.h` 的断言宏定义，不要删除 SPL 断言调用。
- Keil/ARMCC 尚未验证。若 Keil 出现 C99 方言或启动文件差异，单独按 ARMCC 报错处理，不要回退 GNU 已通过的源码。
- `ENABLE_MOTION_DRIVE` 可用 `EXTRA_CFLAGS` 覆盖；默认仍必须为 0，运动构建产物不要覆盖默认 `build/`。

---

## 7. 未验证清单（不要当成已验证）

### 7.1 S31 侧（10 条，详见 `报告/开发报告_S31.md` 第六节）

| # | 项 | 影响 |
|---|---|---|
| 6.1 | `HOTSPOT_RAW_DELTA=1500` 是估计值 | 所有热点判定的准确性挂在这个数上 |
| 6.2 | `raw_to_celsius()` 返回 NAN | 温度永远是 `--` |
| 6.3 | `mag_cali.bin` 下载路径/长度未对实物 | 标定下载可能失败（不影响主流程） |
| 6.4 | SYN6288 字表仅约 70 字，9600 波特率未实测 | 播报缺字 |
| 6.5 | ES8311 24kHz / 30dB 未实测 | 自检峰值可能偏离 |
| 6.6 | GPIO46 是 strapping 脚 | 极端情况下影响启动日志 |
| 6.7 | OV2640 只做探测，未取图 | 无可见光图像 |
| 6.8 | USB 4096B/包、双传输未实测 | 可能超时/丢帧 |
| 6.9 | 备用麦克风宏与摄像头/串口撞脚 | 改开关会静默失效（见 5.4） |
| 6.10 | 无真实 `calib.csv` 样本 | 区域名全落 `UNKNOWN` |

### 7.2 STM32 侧（8 条，详见 `报告/开发报告_STM32.md` 第六节）

最关键边界：

- **GNU Arm 已编译/链接通过，但 Keil/ARMCC 与实机未验证**；不要把 0 error / 0 warning 外推成 LCD、串口 DMA 或运动功能已正常。
- **中文字模已有 76 个非空项**，未收录汉字才降级为 `?`；方向、位序和可读性仍需 `lcdtest` 上屏确认，且尚非完整 500 字库。

---

## 8. 硬约束与禁止事项

### 8.1 环境

1. **工程路径必须是纯 ASCII**。`thermal_robot` 可以，`热成像` 不行。
2. **`esp32s31` 是 preview target**，所有 `idf.py` 带 `--preview`。
3. **并行度用环境变量** `IDF_PY_BUILD_JOBS=3`，不要用 `-j`（本机实测非法）。
4. **构建前必须激活 IDF profile**，且只能用 PowerShell（本机执行策略 Restricted，需进程级 Bypass）。
5. **不联网拉组件**：`IDF_COMPONENT_MANAGER=0` + 本地 `managed_components`。

### 8.2 代码

1. **不要伪造确定值**。`raw_to_celsius()` 返回 NAN、`HOTSPOT_RAW_DELTA=1500` 标着「待标定」，这些是有意保留的诚实状态。76 个已有中文字模不得回退为 NULL/全 0；缺少真实数据时不得填假温度或假标定常量。
2. **不要引入 cJSON 或任何 JSON 库**（S31 侧无 `json` 组件；STM32 侧提示词明确禁止且 20KB RAM 也放不下动态分配）。
3. **STM32 侧不要用 `malloc`/`free`**。全部静态缓冲，启动文件 `Heap_Size` 可置 0。
4. **不要分配整屏缓冲**（240×320×2 = 153,600 B，是 20 KB RAM 的 7.5 倍）。只能逐行推。
5. **不要开 `ENABLE_MOTION_DRIVE`**，除非硬件（TB6612 + 电机 + 编码器 + INA219）已接好且机械常量已实测。当前采购清单里没有这些。
6. **引脚只改 `bsp_pinmap.h` / `board_config.h`**。
7. **单帧 JSON 上限 512 B**，两侧宏同步改。

### 8.3 交付

1. 改完 S31 代码必须重新 `idf.py build` 到 `Project build complete`，不能只改不验。
2. 改完要让 `S31-main1\main\` 下的归档源码同步（见第 9 节），否则会和真源分叉。**落盘前先确认目标目录当前名称。**
3. 新增的不确定项要写进 `报告/` 下的对应开发报告，不要只在代码注释里留一句。
4. **报告统一放 `报告/`，不要再放入 `main/`。** `main/` 只保留源码和构建文件。

---

## 9. 同步归档副本

`S31-main1\main\` 下的是只读源码副本。真源改动后，用这条命令刷一次：

```bash
cd "C:/Users/yyn/Desktop"
cp thermal_robot/main/*.c thermal_robot/main/*.h thermal_robot/main/CMakeLists.txt \
   thermal_robot/main/idf_component.yml "S31-main1/main/ESP32-S31/main/"
cp thermal_robot/CMakeLists.txt thermal_robot/partitions.csv thermal_robot/sdkconfig.defaults \
   "S31-main1/main/ESP32-S31/"
cp thermal_robot/stm32/src/*.c "S31-main1/main/STM32F103CBT6/src/"
cp thermal_robot/stm32/inc/*.h "S31-main1/main/STM32F103CBT6/inc/"
cp thermal_robot/stm32/Makefile thermal_robot/stm32/stm32f103cb.ld \
   "S31-main1/main/STM32F103CBT6/"
cp thermal_robot/stm32/PROJECT_SETUP.md "S31-main1/报告/STM32工程建立与上电验证.md"
```

`源码_01/02_*.md` 两个汇编文件如果要同步，重新按「路径标题 + 代码块」的格式生成即可，格式见文件自身的开头。

---

## 10. 拿到报错后的标准动作

给下一个 AI 的流程建议，按顺序执行：

1. **先判断是哪个阶段**：配置（CMake/组件）→ 编译（编译器）→ 链接（`undefined reference`）→ 运行（串口日志）。阶段不同，排查路径完全不同。
2. **贴全信息再动手**：完整报错文本、文件名、行号、以及**上一轮改了什么**。缺这些就开始改，等于盲改。
3. **查本文第 4 节和第 6 节**。本轮已经踩过的坑，80% 的新报错会落在这里。
4. **确认改的是 `thermal_robot`**（第 0.1 节）。改归档副本是无效动作。
5. **一次只改一个方向，改完立即重编**。多个改动一起上，报错互相掩盖。
6. **不要用"降级实现"绕过报错**。典型错误：删掉 SD 代码让 SDMMC 报错消失、把 `-Werror` 去掉让 const 警告消失、把温度写死 25.0 让 NAN 消失。这些都会把可检测的问题变成隐藏问题。
7. **改完验证并记录**：S31 必须到 `Project build complete`；新发现的坑追加到本文第 4 节，新的不确定项追加到第 7 节。

---

## 11. 事实来源索引

| 想查什么 | 去哪 |
|---|---|
| 需求原文（S31） | `提示词_01_ESP32-S31.md` |
| 需求原文（STM32） | `提示词_02_STM32F103CBT6.md` |
| S31 编译实测、5 个报错、10 条不确定项、引脚表 | `报告/开发报告_S31.md` |
| STM32 云端构建实测、历史预算与实机待验证项 | `报告/开发报告_STM32.md` |
| STM32 本次云端构建命令、尺寸、哈希与验证边界 | `报告/STM32云端编译报告.md` |
| 源码全文（带路径标题） | `源码_01_ESP32-S31.md`、`源码_02_STM32F103CBT6.md`（首次汇编，最新代码看散文件） |
| STM32 GNU/Keil 建工程清单、上电验证顺序 | `报告/STM32工程建立与上电验证.md` |
| S31 成功构建日志 | `build_logs\s31_build_07.log` |
| STM32 默认/运动配置成功日志 | `build_logs\stm32_build_04_default.log`、`build_logs\stm32_build_04_motion.log` |
| STM32 默认固件与 ELF/HEX/MAP | `firmware\STM32F103CBT6\` |
| S31 失败轮原始报错 | `s31_build_02~05.log`（依次对应 SDMMC / const / console / 成功前一轮） |
| IDF 自身 stdout/stderr | `thermal_robot\build\log\idf_py_stdout_output_*.txt` |

---

最新状态（2026-11-19 云端构建后）：S31 ESP-IDF 编译通过；STM32 默认与运动配置均已通过 ARM GCC 编译/链接，且为 0 error / 0 warning。两侧仍未烧录、未接实物、未做整机联调。凡本文写「已实现」的，都不自动代表硬件实测通过。
