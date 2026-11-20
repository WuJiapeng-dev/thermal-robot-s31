# 协作须知（CONTRIBUTING）

本仓库是两人以上协作的固件工程，**编译通过不等于实机验证通过**。改代码前请先读完本文，能省掉大部分返工。

---

## 1. 先跑起来（10 分钟）

```powershell
git clone https://github.com/WuJiapeng-dev/thermal-robot-s31.git
cd thermal-robot-s31
```

### ESP32-S31 侧

前置：ESP-IDF **v6.1**（不是 v5.x），且 **clone 路径必须纯 ASCII**（不要放在含中文的目录下，IDF 6.1 的构建链会在 `cannot read spec file` 或 GBK 解码处直接失败）。

```powershell
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"   # 路径按自己机器改
cd main/ESP32-S31
$env:IDF_PY_BUILD_JOBS = "3"
idf.py --preview build
```

- `esp32s31` 在 IDF 6.1 里是 **preview target**，`idf.py` **必须带 `--preview`**，漏掉会报 unknown target。
- 首次构建需联网：组件管理器会拉 `espressif/usb 1.6.0` 与 `espressif/esp_codec_dev 1.6.2` 到工程根的 `managed_components/`。
- 不要在仓库根目录跑 `idf.py`——那里没有工程文件，只会在根下生成一个巨大的 `build/`。

### STM32F103CBT6 侧

需要 `arm-none-eabi-gcc`，以及自备 **STM32F10x SPL V3.5.0**（受 ST 许可限制，不随仓库分发）：

```bash
cd main/STM32F103CBT6
make SPL_DIR=/path/to/STM32F10x_StdPeriph_Lib_V3.5.0
```

---

## 2. 谁改哪里

| 目录 / 文件 | 归属 | 说明 |
|---|---|---|
| `main/ESP32-S31/main/` | S31 负责人 | 采集、热点、语音、SD、上行链路 |
| `main/STM32F103CBT6/` | STM32 负责人 | 屏显、按键、运动、串口协议解析 |
| `main/ESP32-S31/main/bsp_pinmap.h` | **改线才改** | S31 全部引脚唯一定义处 |
| `main/STM32F103CBT6/inc/board_config.h` | **改线才改** | STM32 全部引脚唯一定义处 |
| 两侧 JSON 帧字段 | **双方共同** | 改字段必须同时改对面的解析，并在 PR 里写明 |

串口协议是对接面，任何字段增删都属于**破坏性变更**：先在对端加兼容分支，再移除旧字段。

---

## 3. 分支与提交

- `main` 保持随时可编译。**不要直接往 `main` 推**。
- 开分支：`feat/xxx`、`fix/xxx`、`docs/xxx`，完成后开 PR，至少一位队友 review。
- 提交信息写清楚**改了什么、为什么**，例如：
  `fix(stm32): 修正 motor.c 中 brake/coast 语义颠倒`
- 一次提交只做一件事，不要把格式化和逻辑修改混在一起。

---

## 4. 不要提交的东西

已在 `.gitignore` 内：`build/`、`build_gnu/`、`spl/`、`*.o`、`*.d`。

还需要人工注意：

- `main/ESP32-S31/managed_components/` —— 组件管理器下载的第三方源码，**不要提交**，否则体积暴涨且版本失控。
- `build_logs/` 里是**历史真实编译日志**，属于证据，新增日志请另起文件名，不要覆盖旧的。
- `firmware/STM32F103CBT6/` 下的 `.elf/.hex/.bin/.map` 是**刻意提交的正式产物**，更新固件时请一并更新并在 PR 说明版本。

---

## 5. 几个「看着像 bug，其实别改」的地方

| 位置 | 现象 | 原因 |
|---|---|---|
| `app_audio.c` 里 `esp_codec_dev` 相关结构体去掉了 `const` | 编译器/IDE 提示可以加 const | **故意去掉的**，加回 `const` 在 IDF 6.1 + esp_codec_dev 1.6.2 下编不过 |
| `stm32/src/motor.c` 的 `MOTOR_DIR_*` 真值表 | 看起来可以简化成按符号位取反 | 按方案 5.5 逐脚写死，**别用 `sign>=0` 判断**——上一版就是这么写导致 brake/coast 互换，上电即危险 |
| `HOTSPOT_RAW_DELTA = 1500` | 想改成"8°C 对应值" | 当前没有黑体标定，1500 是估计的 raw counts 差值，**不是温度换算结果**，改动前先做标定 |
| `raw_to_celsius()` 返回 `NAN`，帧里温度填 `-273.0` | 看起来像没写完 | 缺标定表时的**刻意哨兵**，非法值不能序列化成 `nan`（JSON 不合法） |
| `app_thermal.c` 的 `copy_slot()` 内部不加锁 | 想给每个 slot 加锁 | 外层已持锁，内部再加会**自死锁** |

---

## 6. 提交前自检

1. 改的是哪一侧？另一侧要不要同步改？
2. 有没有把**未实测**写成"已验证"？编译通过只能写"编译通过"。
3. 引脚、协议、常量有没有偏离方案说明书？偏离了要在 PR 里写理由。
4. STM32 侧改动是否影响 20KB RAM / 64KB Flash 余量？（当前约 7KB RAM，Flash 估 31–59KB）
5. 新增文件是否落在正确的子目录，没有在仓库根新建工程目录？

---

## 7. 已知限制不是待修 bug

`README.md` 的「已知限制」表列了 7 项（温度换算、热点标定、中文字模、绝对定位、实机验证、运动模块、WiFi 上传）。这些是**规划内的未完成项**，不要当成缺陷提单，除非你打算当次就实现它。
