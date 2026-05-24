# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

SiFli SDK 是基于 RT-Thread RTOS 的嵌入式 SDK，面向思澈科技（SiFli）SF32LB52x/55x/56x/58x 系列 Cortex-M33 芯片。本仓库主要用于 **SF32LB52 智能手表** 固件开发（`app/watch/`）。

## 常用构建命令

### 环境初始化（每次新 shell 必须执行）

```bash
# 在 SDK 根目录执行，必须用 source
source ./export.sh
# 或
. ./export.sh
```

**注意**：不能用 `bash export.sh`，否则环境变量只作用于子 shell。

### 编译手表固件

```bash
# 方式一：一键编译+烧录（根目录）
bash make.sh

# 方式二：仅编译
source ./export.sh
cd app/watch/project
scons --board=sf32lb52-watch -j16

# 清理
scons --board=sf32lb52-watch -c
```

编译产物位于 `app/watch/project/build_sf32lb52-watch_hcpu/`，包含：
- `bf0_ap.elf` / `main.bin` — 主固件
- `bootloader/bootloader.bin` — 引导程序
- `ftab/ftab.bin` — Flash 分区表

### 烧录（UART 方式）

```bash
# 在 app/watch/project 目录下
sftool -p /dev/ttyACM0 -c SF32LB52 -b 1000000 -m nor write_flash --verify \
  "build_sf32lb52-watch_hcpu/ftab/ftab.bin@0x12000000" \
  "build_sf32lb52-watch_hcpu/bootloader/bootloader.bin@0x12010000" \
  "build_sf32lb52-watch_hcpu/main.bin@0x12020000"
```

Flash 地址：`ftab@0x12000000`，`bootloader@0x12010000`，`main@0x12020000`。

### 生成 IDE 工程

```bash
scons --board=sf32lb52-watch --target=mdk5   # Keil MDK5
scons --board=sf32lb52-watch --target=iar     # IAR
scons --board=sf32lb52-watch --target=eclipse # Eclipse
```

### Menuconfig 配置

```bash
# 使用 sdk.py（推荐）
python tools/sdk.py menuconfig --board=sf32lb52-watch
```

配置层级：Kconfig 默认值 → `customer/boards/<board>/hcpu/board.conf` → `app/watch/project/proj.conf`，合并生成 `.config` 和 `rtconfig.h`。

## 编译注意事项

- **不要** `scons ... | tail -N`：管道中的 `tail` 会等待编译全部结束才输出，导致看起来像超时卡死。用裸 `scons` 或 `scons ... 2>&1 | tee build.log`。
- 手表工程全量编译时间较长，自动化执行时需预留 **≥ 10 分钟**超时。

## 架构总览

```
app/watch/                      ← 手表应用（当前主要工作区）
├── project/                    ← SCons 构建入口（SConstruct, proj.conf）
└── src/
    ├── gui_apps/               ← GUI 功能模块（clock, heart_rate, compass, eez 等）
    ├── device_manager/         ← 设备管理（时间、振动、心率消息队列）
    ├── resource/               ← 资源文件（images, fonts, strings 多语言）
    ├── app_utils/              ← 应用工具（主入口、支付等）
    └── widget/                 ← 自定义控件
```

### SDK 核心目录

| 目录 | 说明 |
|------|------|
| `drivers/` | CMSIS 芯片定义、HAL 实现、RT-Thread 驱动适配 |
| `rtos/` | RT-Thread 内核（默认）、FreeRTOS、OS 抽象层 |
| `middleware/` | 思澈自研组件（audio、bluetooth、app_fwk 应用框架、battery、boot） |
| `external/` | 第三方库（LVGL v8/v9、mbedTLS、ffmpeg、freetype、FlashDB 等） |
| `customer/boards/` | 板级 BSP 配置（72+ 开发板），含 `board.conf`、`rtconfig.py`、`ptab.json` |
| `customer/peripherals/` | 板级外设驱动（传感器、显示等） |
| `tools/build/` | SCons 构建引擎核心（`building.py`：`PrepareEnv()`、`SifliEnv()`、`DoBuilding()`） |

### 双核架构

- **HCPU**（高性能核，240MHz）— 主应用核心，手表应用运行于此
- **LCPU**（低功耗核，48MHz）— BLE 协议栈运行核心

板型名含 `_hcpu` / `_lcpu` 后缀可分别编译两个核心，`sf32lb52-watch` 默认编译 HCPU。

### GUI 开发

- **LVGL** 为 UI 框架（默认使用 LVGL v9，通过 Kconfig `PKG_USING_LITTLEVGL2RTT` 控制）
- **EEZ 流程**：`app/watch/src/gui_apps/eez/` — 使用 EEZ Studio 可视化设计工具，生成 `screens.c/h`、`ui.c`、`actions.h`、`images.c/h`、`fonts.h`
- GUI 模块通过各自的 `SConscript` 注册到构建系统

### Kconfig 配置系统

根 `Kconfig` 提供芯片选型（`SOC_SF32LB52X` 等），子 Kconfig 从 `customer/boards/`、`rtos/`、`middleware/`、`external/` 递归加载。项目通过 `proj.conf` 和 `board.conf` 覆盖默认值。

## 工具链

- **GCC**（Linux/WSL 默认）：`arm-none-eabi-gcc` 14.2.1，`-mcpu=Cortex-M33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard`
- **Keil ARMCC**（Windows 默认）：`armclang` v6，最低 Keil V5.32
- **IAR**：完整支持

通过 `RTT_CC` 环境变量选择（`gcc`/`keil`/`iar`），`RTT_EXEC_PATH` 指定工具链路径。

## 串口调试

| 场景 | 命令 |
|------|------|
| 交互式 MSH Shell | `python3 scripts/sifli_serial_assistant.py -p /dev/ttyACM0 -b 1000000` |
| 批量发送 MSH 命令 | `python3 .cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_batch.py` |
| 原始字节透传 | `python3 .cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_debug.py` |

串口参数可通过环境变量覆盖：`SIFLI_SERIAL_PORT`（默认 `/dev/ttyACM0`）、`SIFLI_SERIAL_BAUD`（默认 `1000000`）。

## GH3018 心率传感器调试

实现：`customer/peripherals/sensor/gh3018/gh3018.c`

Finsh/MSH 命令：
- `hbd -open` — 初始化 GH3018 模块
- `hbd -hb` — 启动心率测量模式
- `hbd -peek` — 读取缓存的 BPM 值
- `hbd -stop` — 停止模块

问题排查参考：`app/watch/doc/gh3018-algo-memory-error-retrospective.md`

## 代码风格

- `.clang-format`：基于 LLVM 风格，4 空格缩进，Allman 花括号，禁用 include 排序
- 代码注释使用中文
- Commit 格式：`[bug|new|opt|chore][module_name] 描述`
