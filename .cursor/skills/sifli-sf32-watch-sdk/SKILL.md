---
name: sifli-sf32-watch-sdk
description: Builds, flashes, and debugs the SiFli SF32LB52 watch firmware via source export.sh, scons in app/watch/project, sftool write_flash, make.sh, serial scripts (batch/assistant/debug), and GH3018 hbd. Use for sf32lb52-watch, compile timeouts (avoid scons|tail), UART/MSH, GH3018, or MEMORY ERROR / algo pool issues.
---

# SiFli SF32 手表工程（本仓库）

## 工程要点

| 项目 | 说明 |
|------|------|
| SDK 根目录 | 仓库根目录（含 `tools/`、`rtos/`、`app/`） |
| 当前手表应用 | `app/watch/`（LVGL 界面，见 `app/watch/README.md`） |
| 构建工程目录 | `app/watch/project/` |
| 板型 | `sf32lb52-watch`（`scons --board=sf32lb52-watch`） |
| 芯片系列 | `SF32LB52`（`sftool -c SF32LB52`） |
| 一键编译烧录 | 仓库根目录 `make.sh`：激活 Python 环境 → `scons` → `sftool write_flash` |

**串口与波特率（与 `make.sh` 中 `sftool`、固件默认串口一致，以实际文件为准）**

- 设备：常见为 `/dev/ttyACM0`（CDC）；也可能是 `ttyUSB0` 等。
- 波特率：`1000000`（1 Mbps）。

换机或换端口时：同步修改 `make.sh` 里的 `sftool -p`，并用环境变量覆盖串口脚本（见下文）。

**环境**

- **推荐（可移植）**：在 **SDK 根目录** 执行 `source ./export.sh`（或 `. ./export.sh`）。该脚本会检测 bash/zsh、调用 `tools/activate.py --export` 并 `eval` 导出变量，与仓库内机制一致；**必须 source，不要 `bash export.sh`**（否则环境只作用于子 shell）。
- **等价方式**：`eval "$(<venv 或系统 python> tools/activate.py --export --shell bash)"`（zsh 用 `--shell zsh`）。仓库根目录的 `make.sh` 若写死了本机 Python/SDK 路径，换机后需改路径或改用 `export.sh`。
- 依赖：`scons`、`sftool`（激活后可用）；Python 建议安装 `pyserial`（`tools/requirements/requirements.core.txt`）。

**烧录镜像与地址（与当前 `make.sh` 中 `write_flash` 一致）**

- `ftab.bin` → `0x12000000`
- `bootloader.bin` → `0x12010000`
- `main.bin` → `0x12020000`

产物目录：`app/watch/project/build_sf32lb52-watch_hcpu/`。

**GH3018 / 算法内存复盘（详细）**

- 见 `app/watch/doc/gh3018-algo-memory-error-retrospective.md`。

---

## 编译与烧录

1. 连接开发板 USB；Linux 下确保对串口设备有读写权限（`dialout` 或 udev 规则）。
2. 在**仓库根目录**执行：

```bash
bash make.sh
```

3. 逻辑概要：激活环境 → `cd app/watch/project` → `scons --board=sf32lb52-watch -j16` → `sftool ... write_flash ... --verify`。

**仅编译（不烧录）**

```bash
cd /path/to/SiFli-SDK
. ./export.sh
cd app/watch/project
scons --board=sf32lb52-watch -j16
```

**烧录（示例，与常见 `make.sh` / IDE 任务一致）**

在已 `source export.sh` 的同一 shell 中：

```bash
sftool -p /dev/ttyACM0 -c SF32LB52 -b 1000000 -m nor write_flash --verify \
  "build_sf32lb52-watch_hcpu/ftab/ftab.bin@0x12000000" \
  "build_sf32lb52-watch_hcpu/bootloader/bootloader.bin@0x12010000" \
  "build_sf32lb52-watch_hcpu/main.bin@0x12020000"
```

（若在 `app/watch/project` 下执行，可用上述相对路径；否则写全路径到 `build_sf32lb52-watch_hcpu/...`。）

**为何 Agent/自动化里编译像「超时」、一直无输出**

- **`scons ... | tail -80` 尤其有害**：对管道上的 `tail -N`，常见实现要等上游 **进程结束** 才能确定「最后 N 行」，因此 **整趟编译完成前几乎没有任何输出**，再叠加 Cursor 默认命令超时，会被误判为卡死。
- 一般也不要把长时间运行的 `scons` 接到只消费末尾的管道；stdout 接到管道时子进程还可能 **块缓冲**，进一步推迟可见输出。
- **正确做法**：直接运行 `scons ...`；若需留档可用 `scons ... 2>&1 | tee build.log`（避免再用 `tail` 截断长时间任务）；或后台跑 `scons` 并读终端/日志文件。
- 给自动化预留足够等待时间（例如 **≥ 10 分钟**），手表工程全量编译通常远长于 3 分钟。

---

## 串口调试脚本一览

| 脚本 | 路径 | 用途 |
|------|------|------|
| 原始字节透传 | `.cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_debug.py` | 日志到 stdout，键盘输入进串口；适合人工 Finsh/MSH |
| 批量 MSH 命令 | `.cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_batch.py` | 依次发送命令并采集回显；适合自动化、CI |
| 交互式助手 | `scripts/sifli_serial_assistant.py` | 逐行输入命令、带历史；适合长时间调试 |
| GH3018 一键闭环 | `scripts/gh3018_full_pipeline.sh` | `make.sh` + 等待枚举 + `sifli_serial_batch.py` 执行 `hbd -open/-hb/-peek/-stop` |

**环境变量（覆盖串口参数，无需改脚本）**

- `SIFLI_SERIAL_PORT`：默认 `/dev/ttyACM0`
- `SIFLI_SERIAL_BAUD`：默认 `1000000`
- `SIFLI_SERIAL_PULSE_DTR`：若设置（任意非空），`sifli_serial_batch.py` 会加 `--pulse-dtr`（部分转串口可用 DTR 脉冲复位 MCU；按板子慎用）

**批量脚本要点（`sifli_serial_batch.py`）**

- 打开串口后**将 DTR/RTS 置低**，减少 Linux CDC 默认状态导致「只有日志、命令无回显」。
- **默认不要** `--flush-rx`：清空 RX 可能破坏 MSH 正在接收的命令缓冲。
- 日志密集时加大 `--silence`、`--max-wait`；首条阻塞命令用 `--min-capture`（逗号分隔，与命令顺序对应），例如 `"10,50,0,0"` 表示第 2 条命令至少采集约 50s 后才允许因静默结束。
- 烧录后 USB 重枚举：加大 `--settle`（如 4s）或 `--boot-wait`。

**透传调试示例**

```bash
python3 .cursor/skills/sifli-sf32-watch-sdk/scripts/sifli_serial_debug.py -p /dev/ttyACM0 -b 1000000
```

**GH3018 自动化示例（仓库根目录）**

```bash
bash scripts/gh3018_full_pipeline.sh
```

注意：烧录与串口脚本**不要同时独占**同一端口；先停一个再开另一个。

---

## GH3018 调试速查（Finsh：`hbd`）

常用子命令（实现见 `customer/peripherals/sensor/gh3018/gh3018.c`）：

- `hbd -open`：`init_gh3018_sensor()`（初始化模块）
- `hbd -hb`：`set_gh3018_hr_mode()`（心率模式）
- `hbd -peek`：打印 `gh3018_get_hr()` 缓存 BPM
- `hbd -stop`：`gh30x_module_stop()`

**心率数据为何能算出来但 `peek` 曾为 0**

- `__GET_RAWDATA_WITHOUT_ALGO_HANDLE == 1` 时，心率走 **Goodix demo** 路径 `GH30xHrAlgoExe()`，**不经过** `gh3011_algo_calculate_hook()`。
- 应用缓存需在 **`GH30X_HrAlgorithmResultReport()`**（`customer/peripherals/sensor/gh30x_algo_demo/common/src/gh30x_demo_algo_hook.c`）中调用 `gh3018_set_hr()`，与算法日志 `[GH30xHrAlgoExe] hr = ...` 对齐。

---

## Agent 执行建议

- **加载环境**：优先在 SDK 根目录 `source ./export.sh`，再 `cd app/watch/project` 执行 `scons`/`sftool`；避免依赖未改路径的 `make.sh`（其中可能硬编码他机 Python 路径）。
- **编译 + 烧录**：`bash make.sh`（已按本机修好路径时）或：`source export.sh` → `scons` → `sftool write_flash ...`。
- **在 Cursor 里跑编译**：**不要** `scons ... | tail -80` 这类管道；用裸 `scons` 或 `| tee`，并把等待上限调到 **600000 ms 量级**（或后台运行 + 读终端日志文件）。
- **只看串口 / 手动 MSH**：`sifli_serial_debug.py` 或 `scripts/sifli_serial_assistant.py`。
- **自动化串口 / 回归 GH3018**：`scripts/gh3018_full_pipeline.sh` 或直接用 `sifli_serial_batch.py` 传命令列表。
- **改表盘 / GUI**：`app/watch/src/`，EEZ 工程见 `app/watch/src/gui_apps/eez/` 与 `app/watch/README.md`。
- **GH3018 崩溃、algo mem pool、MEMORY ERROR**：先读 `app/watch/doc/gh3018-algo-memory-error-retrospective.md`，再查 `gh30x_demo_algo_memory.c/.h`、`gh30x_example_config.h`。
