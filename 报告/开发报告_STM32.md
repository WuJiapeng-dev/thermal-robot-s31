# 开发报告 · STM32F103CBT6 显示与通信侧

## 2026-11-19 二次补充（当前状态优先看本节）

- **中文字库**：`font12.c` 已生成 76 个非空 12×12 字模，每字 24B；Unicode/GB2312、位宽及非空数据静态检查通过。上屏方向仍需 `lcdtest` 实物确认，尚非完整 500 字库。
- **串口修复**：DMA 乒乓接收现在同时切换 `CMAR` 与 `CNDTR`，满缓冲帧按潜在溢出丢弃，避免覆盖主循环正在解析的数据。
- **停走协议**：增加 STM32→S31 `capture` 和 S31→STM32 `capture_done`；`motion_step_scan()` 停稳后请求两帧，并核对响应序号与实际采集帧数。`ENABLE_MOTION_DRIVE` 默认仍为 `0`，启用电机前必须完成安全调试。
- **显示**：增加 `DT`、置信度和热点面积显示；补充“知、域”字模以支持未知区域。
- **运动修复**：纠正满电 85%/低压恢复 100% 的限幅反向问题；PI 实际速度改为当前控制周期的脉冲增量/时间，不再使用错误量纲的全程估算。
- **云端真实构建**：Linux 云主机使用 `arm-none-eabi-gcc 10.3.1`、SPL V3.5.0、本目录 `Makefile` 与 `stm32f103cb.ld` 完成编译和链接。默认配置与 `ENABLE_MOTION_DRIVE=1` 配置均为 **0 error / 0 warning**。
- **默认固件**：`text=37820`、`data=500`、`bss=7052`；BIN 为 **38,324 字节**，SHA-256：`aff43864fec650792abef296c5c9b47b7e6dda8ae8beca4ea173665b6b0762a7`。
- **运动配置验证**：`text=42324`、`data=500`、`bss=7100`，仅用于确认启用分支可编译；运动版 BIN 不作为交付烧录文件。
- **构建修复**：补齐 `assert_param` 配置；清理默认关闭运动时的未使用代码警告；扩大 UI 状态行缓冲；`ENABLE_MOTION_DRIVE` 支持构建参数覆盖且默认仍为 `0`。
- **证据与产物**：`build_logs/stm32_build_04_default.log`、`build_logs/stm32_build_04_motion.log`、`firmware/STM32F103CBT6/`。
- **验证边界**：以上是 ARM GCC 编译、链接、ELF 入口与向量表检查通过，不代表 Keil/ARMCC、烧录、串口、LCD、DMA、运动或整机联调通过。

> 下文第一至九节保留首次报告的 Windows 本机探测、估算和旧字库状态作为历史记录；当前状态以本节为准。


生成时间：2026-11-19 15:40；云端构建更新：2026-11-19
依据：`提示词_02_STM32F103CBT6.md`
工程目录：`C:\Users\yyn\Desktop\thermal_robot\stm32`（源码 + 建工程清单，不含 `.uvprojx`）
库：STM32F10x SPL V3.5.0（不用 HAL / CubeMX / cJSON）

---

## 一、首次报告结论（历史存档）

**当时 Windows 本机未编译。** 本机没有 ARM 工具链（无 Keil MDK，无 `arm-none-eabi-gcc`）。下面的 Flash/RAM 数字是当时按源码逐项算出的预算，不是编译器输出；最新云端实测结果见本文顶部。

代码本身做了两项不需要编译器的静态核对（见第三节），无缺失、无悬空声明。

---

## 二、编译状态（真实探测过程与结果）

### 2.1 探测命令与输出

| 探测 | 命令 | 结果 |
|---|---|---|
| PATH 里的 ARM GCC | `which arm-none-eabi-gcc` | 无输出（未安装） |
| Keil MDK | `ls C:/Keil_v5`、`ls C:/Keil`、`ls -d /c/Keil* /d/Keil*` | 全部 `No such file or directory` |
| ARM 官方工具链 | `ls "C:/Program Files/ARM"`、`ls "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi"` | 不存在 |
| 其他程序目录 | `ls "C:/Users/yyn/AppData/Local/Programs"` | 只有 `Common` |

结论：**本机不具备编译 STM32 工程的任何工具链。** 未做任何安装动作（提示词只授权了 S31 侧跑编译，STM32 侧连这个授权都没有）。

### 2.2 因此以下均为「未验证」

- 语法/类型错误是否为零
- Flash / RAM 实际占用
- 链接脚本（`STM32F10x_MD` 散列文件）是否够用
- 中断向量、`SysTick` 优先级、USART2 DMA 通道映射是否如代码所写

要变成「已验证」，需要你在装好 Keil MDK5 + SPL 的机器上，按 `stm32/PROJECT_SETUP.md` 建工程并编译一次，把 Build Output 的 `Program Size: Code=… RO-data=… RW-data=… ZI-data=…` 贴回来。

---

## 三、已经做过的静态核对（不依赖编译器）

1. **声明/定义一致性**：用脚本把 `inc/*.h` 里所有函数声明与 `src/*.c` 里所有函数定义做交叉比对——**声明 66 个，定义 105 个（含 static 内部函数），缺失定义 0 个**。也就是说头文件里承诺的接口都有实现，不会出现链接期 `undefined symbol`（前提是建工程时源文件没漏加）。
2. **入口唯一**：全目录仅 `src/app.c:96` 有一处 `int main(void)`，没有第二个 `main`。

其余（语法、类型、寄存器位定义）只能等编译器。

---

## 四、Flash / RAM 预算（逐项算，不是实测）

### 4.1 Flash（128KB）

| 项 | 预算（提示词给的） | 本工程 | 说明 |
|---|---|---|---|
| 程序（含 SPL + CMSIS + 启动文件） | 55–70 KB | **估算 30–45 KB** | 本工程 15 个 `.c` 共约 2,570 行；SPL 只用了 adc/dma/gpio/i2c/rcc/spi/tim/usart + misc 共 9 个模块。ARMCC 默认不优化时取上限，开 `-O1` 取下限。**这是最不可靠的一个数** |
| 12×12 中文字库（500 字） | ≈15 KB | **设计值 12.6 KB**（500×24 B = 12,000 B + 目录表 72×8 = 576 B） | 当前 bitmap 全为 `NULL`，实际只占 576 B + 少量字符串 |
| ASCII 5×7 字模 | — | 480 B | `g_ascii5x7[96][5]`，const，进 Flash |
| 界面常量字符串 | 预留 30 KB | 约 200 B | 状态/标签字符串，远没用满 |
| **合计** | 100–115 KB | **估算 31–59 KB** | 剩 69–97 KB 余量 |

结论：即便按最坏情况（45 + 13 + 0.5 ≈ 59 KB），**128KB Flash 仍有约 69 KB 余量**，加一层中文界面贴图或第二套字库都放得下。

### 4.2 RAM（20KB = 20,480 B）

| 项 | 字节 | 来源 |
|---|---|---|
| USART2 双接收缓冲 | 1,026 | `comm.c: g_rx_buffer[2][513]` |
| 调试台最近 5 帧 JSON 历史 | 2,565 | `debug_console.c: g_history[5][513]` |
| 调试台行长度数组 + 行缓冲 | 106 | `g_history_length[5]`(10) + `line[96]` |
| LCD 单行缓冲（240×2） | 480 | `ili9341.c: g_lcd_line[480]` |
| UI 文本缓存（群名/中文名/状态） | 176 | `ui.c: 32 + 48 + 96` |
| 各模块状态标志、计数器、运动控制积分量 | 约 120 | app/board/comm/encoder/motion/ina219 |
| **静态合计（.data + .bss）** | **约 4,473 B（≈4.4 KB）** | — |
| 主栈（建议 `Stack_Size=0x800`） | 2,048 | 见 `PROJECT_SETUP.md` 第 9 节 |
| 堆（`Heap_Size=0x200`，保持默认） | 512 | `printf` 可能用到 |
| **占用合计** | **约 7.0 KB / 20 KB（35%）** | 余约 13 KB |

关键约束核对：**没有分配整屏缓冲**。240×320×2 = 153,600 B 是 20KB 的 7.5 倍，本工程只保留一行 480 B 的 `g_lcd_line`，逐行推给 SPI，符合提示词「绝对不允许分配整屏缓冲」。

栈的实测风险点：`app.c` 主循环里有一个栈上 `json_frame_t`（144 B），`ui.c` 里有两层各 96 B 的行缓冲，加上浮点格式化——1KB 默认栈会比较紧，所以清单里要求改到 2KB。

---

## 五、实现要点（对应提示词条目）

### 5.1 串口

| 项 | 配置 |
|---|---|
| USART2（接 S31） | PA2/PA3，921600 8N1，**DMA1_Channel6 + IDLE 中断**接收，双 512B 缓冲乒乓切换 |
| 波特率误差 | 72MHz / 78 = 923,076，相对 921,600 误差 **+0.16%**（< 2.5% 上限，可接受） |
| USART1（调试台） | PA9/PA10，115200 8N1，`printf` 重定向到此 |
| 发送 | 轮询 + 超时 `UART_TX_TIMEOUT=100000`，不阻塞等死 |

### 5.2 JSON 解析（`json_frame.c`，手写，无 cJSON）

- 支持三种帧：`hot` / `hb` / `st`，字段与提示词给的单行 UTF-8 JSON 一致。
- `missing_mask` 位掩码逐字段记录「这个字段没来 / 解析失败」，UI 层据此决定显示原值还是占位。
- 缓冲不 malloc、不递归；单帧上限 512 B（与 S31 侧 `LINK_JSON_MAX_BYTES` 对齐）。

### 5.3 显示（`ili9341.c` + `ui.c` + `font12.c`）

- SPI1，**18MHz**（72MHz / 4），Mode0，只写不读；初始化序列：软复位 → Sleep Out → 16bit RGB565 → MADCTL → 开显示。
- 提供 `lcd_fill_rect` / `lcd_draw_string`（ASCII + 中文点阵）/ `lcd_draw_hline`。
- 界面四块：元件群名、温度、状态、电量。
- **温度 -273.0 → 显示 `--`**（与 S31 未标定的约定对齐）。
- 断链 > 3 秒（`LINK_TIMEOUT_MS=3000`）→ 显示「S31 断链」+ 蜂鸣（120ms）。
- 缺字 → `?`，不会花屏；但没有可信字模，**当前所有汉字都走 `?` 降级**，这是本工程最大的功能缺口，见 6.3。

### 5.4 电池与调试台

- 电池：PA4 / ADC1_IN4，100k/33k 分压，9 次采样取中位数，8.4V 满 / 6.4V 空。
- 调试命令：`help` `lcdtest` `link` `beep` `bat` `mtest` `etest` `go` `turn` `scan`；`link` 可回看最近 5 帧原始 JSON。

### 5.5 运动模块（完整预留，默认关闭）

- `ENABLE_MOTION_DRIVE 0`：`motor_init()` / `encoder_init()` / `ina219_init()` 直接返回，不碰任何寄存器。
- 关闭时执行 `mtest/etest/go/turn/scan` 只打印「本功能未启用（采购清单无驱动板）」，不会卡死。
- 打开后：TB6612 双路 PWM（TIM1_CH1/CH4，10kHz），换向先刹车 20ms；满电限占空比 85%（低于 7.4V 恢复 100%）；编码器 TIM2/TIM4 正交解码 + 16 位溢出扩展到 int32；定距/转角用梯形加减速 + PI 速度环。
- 机械常量（`ENCODER_PULSES_PER_REV=6600`、轮周长 157.1mm、轮距 110mm、90° 转角 3630 脉冲）**全部标了「待实测」**，是从常见 11PPR×4×150 减速比推的，不是量出来的。

---

## 六、不确定项清单（8 条）

### 6.1 整份代码没有编译过
- **不确定什么**：是否存在语法/类型错误、SPL 头文件包含顺序问题、ARMCC 与 GCC 的方言差异（如 `for` 内声明、`//` 注释）。
- **影响什么**：可能一次编不过。
- **怎么验证**：按 `PROJECT_SETUP.md` 建工程编译，把 Build Output 贴回来；有错我按报错改。

### 6.2 Flash/RAM 预算是算的，不是编译器给的
- **不确定什么**：真实的 `Code / RO-data / RW-data / ZI-data` 四个数。
- **影响什么**：如果程序段实际超过 70KB（ARMCC 不优化时有可能），加 12.6KB 字库后仍够，但要重新核对。
- **怎么验证**：编译后看 Build Output 的 `Program Size:` 行。

### 6.3 中文字模是空的（最大功能缺口）
- **不确定什么**：没有可信的 12×12 字模来源，`font12.c` 里 72 个字的 `bitmap` 全为 `NULL`，实际显示一律降级成 `?`。
- **影响什么**：界面现在只能看 ASCII；「电源区 / 温度 / 过热」这类中文标签出不来。这是按提示词「字形不确定时宁可只显示 ASCII」的保守选择，但离可用还差一步。
- **怎么验证/补齐**：见第七节的生成思路，用 PIL 渲染出 500 字子集的 24 字节点阵灌进 `g_font12_catalog`，再上屏看是否可读。

### 6.4 字模格式是我定的，没和任何工具链对过
- **不确定什么**：约定是「24 字节/字、逐行 12 行、每行占 2 字节、高字节在前、每行低 4 位无效」。这是 12×12 存在 16 位对齐里的常见做法，但没有现成字库可比对。
- **影响什么**：如果取模工具的位序相反或按列扫描，字会变成「镜像/旋转/错位」。
- **怎么验证**：先只灌「电」「源」两个字，`lcdtest` 上屏肉眼比对，确认位序后再批量灌。

### 6.5 USART2 921600 + DMA + IDLE 的实际稳定性
- **不确定什么**：512 B 缓冲在高频 JSON 下是否会因为 IDLE 中断处理不及时而丢帧；`g_oversize_count` / `g_drop_count` 的真实值。
- **影响什么**：丢帧会让界面停在上一帧，断链计时误触发 3 秒告警。
- **怎么验证**：S31 侧跑 `link_test`（5 个心跳 + 1 个热点帧），STM32 侧用 `link` 命令看最近 5 帧是否完整、以及两个计数器是否增长。

### 6.6 ILI9341 18MHz 与初始化序列
- **不确定什么**：18MHz 在你的排线长度/模块上是否稳定（长排线常见花屏）；初始化序列是按通用 ILI9341 写的，你的模块可能是 ST7735/ILI9488 兼容片。
- **影响什么**：花屏、偏色、方向不对。
- **怎么验证**：先跑 `lcdtest` 看彩条是否纯净、颜色顺序是否 RGB；花屏就把 `LCD_SPI_BAUD_DIV` 从 `SPI_BaudRatePrescaler_4` 改成 `_8`（9MHz）复测；偏色改 MADCTL。

### 6.7 电池分压比与 ADC 基准
- **不确定什么**：100k/33k 是提示词给的，实际板子上是不是这两个电阻、Vref 是否真的 3.3V（用 USB 供电时会偏低）。
- **影响什么**：电量百分比整体偏高或偏低；8.4V 对应 2.082V 落在 3.3V 量程内，本身没问题。
- **怎么验证**：万用表量电池端电压，同时跑 `bat` 命令读显示值，算比例偏差；偏差 >3% 就改 `BATTERY_DIVIDER_TOP_KOHM/BOTTOM_KOHM`。

### 6.8 运动控制的机械常量全是推算值
- **不确定什么**：`ENCODER_PULSES_PER_REV=6600`（11PPR×4×150）、轮周长 157.1mm、轮距 110mm、90° 转角 3630 脉冲——**没有一个是量出来的**。
- **影响什么**：一旦把 `ENABLE_MOTION_DRIVE` 改成 1，走 100mm 可能走成 80mm 或 130mm，转角会累积偏差。
- **怎么验证**：补上驱动板后，先 `go 100` 实测位移反推 `ENCODER_PULSES_PER_MM`，再 `turn 90` 实测角度反推 `ENCODER_TURN_90_PULSES`。

---

## 七、其余字模怎么生成（提示词要求的 Python 思路）

本机没有可信字模，但生成路径是明确的。用 PIL 渲染一个 12×12 点阵，按本工程的 24 字节格式导出：

```python
# 依赖：pip install pillow
# 字体：任意支持 GB2312 的 12px 点阵字体，例如 simsun.ttc（宋体 12 号最接近点阵观感）
from PIL import Image, ImageDraw, ImageFont

FONT = ImageFont.truetype("C:/Windows/Fonts/simsun.ttc", 12)
THRESHOLD = 128          # 二值化阈值，先用 128，上屏后按可读性微调

def glyph_24bytes(ch: str) -> list[int]:
    img = Image.new("L", (12, 12), 0)
    ImageDraw.Draw(img).text((0, 0), ch, fill=255, font=FONT)
    px = img.load()
    out = []
    for y in range(12):                 # 逐行
        hi = lo = 0
        for x in range(12):
            if px[x, y] >= THRESHOLD:   # 亮点 = 笔画
                if x < 8:
                    hi |= 1 << (7 - x)  # 每行前 8 位在高字节
                else:
                    lo |= 1 << (15 - x) # 后 4 位在低字节的高 4 位
        out += [hi, lo]                 # 高字节在前，共 24 字节
    return out

# 用法：对 500 字子集逐个生成，输出成 C 数组，替换 font12.c 里的 NULL
# 每字 24 字节 → 500 字 = 12,000 字节，符合 15KB 预算
```

要点：
1. **先验证位序**：只生成「电」「源」两个字，`lcdtest` 上屏看是不是正的；镜像了就翻 `x`，倒立了就翻 `y`。
2. **阈值**：宋体 12px 在 12×12 画布里笔画会糊，阈值 100~150 之间试，宁可少一笔也不要连成块。
3. **字表来源**：不要手打 500 字。从 `calib.csv` 的 `zh_name` 列 + 界面固定标签（电源、区、温度、正常、异常、过热、电量、巡检、故障、断链、扫描、分析、完成、连接、等待）去重收集，通常 100~200 字就够覆盖实际界面。
4. **GB2312 编码**：`font12_get()` 按 GB2312 双字节查表，生成时用 `ch.encode('gb2312')` 得到两个字节再拼成 `0xB5E7` 这种形式，和现有目录表一致。

---

## 八、建工程与上电验证

完整清单在 `stm32/PROJECT_SETUP.md`：宏（`USE_STDPERIPH_DRIVER`、`STM32F10X_MD`）、15 个源文件、9 个 SPL 源文件、CMSIS/启动文件、4 条包含路径、Keil 设置（8MHz 晶振、SWD 保留 / JTAG 关闭）、`Stack_Size` 改 2KB。

上电验证顺序（同清单第 7 节）：

1. USART1 115200 敲 `help` → 命令表出来
2. `lcdtest` → 彩条 + ASCII；中文位置显示 `?` 属预期
3. `bat` → 电压读数合理
4. 接 S31 交叉线（S31 TX=GPIO46 → PA3，S31 RX=GPIO37 ← PA2，共地）→ 界面刷新
5. `link` → 最近 5 帧 JSON 完整
6. `mtest/etest/go/turn/scan` → 应提示「本功能未启用」

---

## 九、与 S31 侧的接口对齐核对

| 项 | S31 侧 | STM32 侧 | 一致 |
|---|---|---|---|
| 波特率 | `LINK_UART_BAUD 921600` | `COMM_BAUD 921600U` | ✅ |
| 单帧上限 | `LINK_JSON_MAX_BYTES 512` | `COMM_RX_FRAME_MAX 512` | ✅ |
| 心跳周期 | `LINK_HEARTBEAT_PERIOD_MS 1000` | 断链判定 `LINK_TIMEOUT_MS 3000`（3 个周期） | ✅ |
| 温度无效哨兵 | `APP_TEMP_INVALID_C (-273.0f)` | `TEMPERATURE_INVALID (-273.0f)` → 显示 `--` | ✅ |
| 帧类型 | `hot` / `hb` / `st` | `JSON_TYPE_HOT/HB/ST` | ✅ |
| stage 取值 | `idle/scan/analyze/done` | `stage[16]` 字符串直传显示 | ✅ |
| 中文长度 | `zh` ≤12 汉字 | `JSON_ZH_CHARS 48`（12 字 UTF-8 上限） | ✅ |
