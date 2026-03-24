# GH3018 / Goodix 算法内存与「MEMORY ERROR」问题复盘

本文档归纳在本仓库（SiFli SF32 手表 + GH3018）上排查 **算法静态内存池不足**、**池损坏后二次 HardFault**，以及 **`__GET_RAWDATA_WITHOUT_ALGO_HANDLE` 与心率同步路径** 的过程，便于后续同类问题快速定位。

---

## 1. 现象与目标

### 1.1 现象

- 串口或日志中出现 **algo mem pool failure** / 与 Goodix 内存池相关的 **MEMORY ERROR** 类输出（具体字符串以库与宏配置为准）。
- 池失败后若处理不当，可能出现 **HardFault**（例如 MemManage、`DACCVIOL`，`MMAR` 指向异常地址），表现为 `gh3018` 线程或后续算法调用崩溃。
- 部分阶段下算法日志 **`[GH30xHrAlgoExe] hr = xxx, UpdateFlag = 1`** 已出现，但 Shell 命令 **`hbd -peek`** 仍显示 **`cached HR bpm: 0`**。

### 1.2 目标

- 算法在静态池内稳定运行，无池耗尽后的连锁崩溃。
- 佩戴有效时，应用层 **`gh3018_get_hr()`** 与 **`hbd -peek`** 能反映算法输出。

---

## 2. 背景：两套「心率」相关路径

### 2.1 Demo 算法调用链（`__GOODIX_ALGO_CALL_MODE__`）

- 心率运算入口之一：`customer/peripherals/sensor/gh30x_algo_demo/call/src/gh30x_demo_algo_call_hr.c` 中的 **`GH30xHrAlgoExe()`**。
- 该函数调用 **`goodix_hba_update()`**，将结果写入 **`pstFrameInfo->pstAlgoResult`**（含 `snResult[0]`、`uchUpdateFlag`），并调用 **`GH30X_HrAlgorithmResultReport()`**。

### 2.2 HBD Hook 路径

- `customer/peripherals/sensor/gh3018/gh30x_example_hook.c` 中 **`gh3011_algo_calculate_hook()`** 在 **`GH3011_FUNC_OFFSET_HR`** 分支同样调用 **`goodix_hba_update()`**，并在成功时写 **`algo_result`**，历史上曾在此调用 **`gh3018_set_hr()`**。

### 2.3 `__GET_RAWDATA_WITHOUT_ALGO_HANDLE` 的含义（关键）

- 配置位置：`customer/peripherals/sensor/gh3018/gh30x_example_config.h` 中的 **`__GET_RAWDATA_WITHOUT_ALGO_HANDLE`**。
- 设为 **`1`**：与原厂示例一致，**FIFO/原始数据处理路径不再走易引发问题的「无 algo handle」分支**；在本项目上曾用于 **避免 gh3018 线程 HardFault**。
- 副作用：在此模式下，心率实际常由 **Demo 的 `GH30xHrAlgoExe`** 完成，**`gh3011_algo_calculate_hook` 的 HR 分支可能不再参与同步**。若仅在 hook 里调用 **`gh3018_set_hr()`**，则会出现 **算法已打印 HR，但 `peek` 仍为 0**。

**结论（产品化同步点）**：在 **`GH30X_HrAlgorithmResultReport()`**（`customer/peripherals/sensor/gh30x_algo_demo/common/src/gh30x_demo_algo_hook.c`）中，当 **`uchUpdateFlag != 0`** 且 BPM 合理时调用 **`gh3018_set_hr()`**，与 **`GH30xHrAlgoExe`** 的输出对齐。

---

## 3. 静态算法内存池：`GH30X_ALGORITHMS_MEMORY_SIZE_FINAL`

### 3.1 定义位置

- `customer/peripherals/sensor/gh30x_algo_demo/common/inc/gh30x_demo_algo_memory.h`
- 最终大小宏：**`GH30X_ALGORITHMS_MEMORY_SIZE_FINAL`**，由启用的算法类型、通道数、算法版本（TINY / BASIC / 其它）等表格项累加后再对齐。

### 3.2 本仓库采取的措施：表算值 + 额外冗余

在表算结果上增加 **8192 字节** 冗余（再按 4 字节对齐），缓解 **峰值略高于文档表值** 或 **版本差异** 导致的池不足：

```c
#define GH30X_ALGORITHMS_MEMORY_SIZE_FINAL (((GU32)((GH30X_ALGORITHMS_MEMORY + 8192 + 3) / 4)) * 4)
```

（具体以当前头文件为准；若仍报池失败，可在评估 RAM 后继续小幅加大冗余，或考虑动态内存方案。）

### 3.3 静态缓冲区绑定

- `customer/peripherals/sensor/gh30x_algo_demo/common/src/gh30x_demo_algo_memory.c` 中，在 **`__USER_DYNAMIC_ALGO_MEM_EN__` 关闭**时，使用 **`g_punGh30xAlgoMemBuffer[GH30X_ALGORITHMS_MEMORY_SIZE_FINAL / 4]`** 作为池载体，**`g_punGh30xAlgoMemBufferPointer`** 必须指向该数组（曾出现错误初始化导致指针异常，需保证为合法数组首地址）。

---

## 4. 池失败后的行为：`Gh30xPoolIsNotEnough`

### 4.1 问题本质

- Goodix 库在池不足或池状态异常时会进入 **`Gh30xPoolIsNotEnough` / `Gh3x2xPoolIsNotEnough`** 一类回调。
- 若实现为 **简单打印后 return**，库内后续仍可能继续 **malloc/使用已损坏池**，从而 **二次 HardFault**，调试上表现为「先 MEMORY ERROR，再莫名崩溃」。

### 4.2 本仓库策略

- 文件：`gh30x_demo_algo_memory.c`
- 做法：**仅记录一次**有用信息（如调用 **`print_memory_usage()`**、打印明确说明），然后 **长期阻塞当前线程**（例如 **`rt_thread_mdelay` 循环**），**不再 return 到库内错误路径继续跑**。
- 代价：该线程上的传感器/算法任务实质停转，需 **复位或重新上电** 恢复；但避免了 **随机 HardFault** 与日志刷屏，便于现场判断为 **明确的池失败停服**。

---

## 5. 与其它问题的区分

| 症状 | 更可能原因 | 建议 |
|------|------------|------|
| pool failure / MEMORY ERROR | 静态池偏小、指针未指向静态数组、多算法组合峰值 | 加大 `GH30X_ALGORITHMS_MEMORY_SIZE_FINAL`、检查 `g_punGh30xAlgoMemBufferPointer`、查 `Gh30xPoolIsNotEnough` 是否被二次进入 |
| MemManage / DACCVIOL | 空指针、栈溢出、错误 `__GET_RAWDATA_WITHOUT_ALGO_HANDLE` 组合 | 对照 `gh30x_example_config.h`、线程栈（如 gh3018 任务栈加大）、用 fault 寄存器与 map 查 MMAR |
| 有 `[GH30xHrAlgoExe] hr=…` 但 peek=0 | Demo 路径未写 `gh3018_set_hr` | 在 `GH30X_HrAlgorithmResultReport` 同步 `gh3018_set_hr` |
| 串口只有日志、MSH 无回显 | CDC DTR/RTS、烧录占口、错误 flush RX | 使用 `sifli_serial_batch.py` 默认 DTR/RTS 拉低；勿滥用 `reset_input_buffer` |

---

## 6. 调试与闭环脚本（与 Skill 一致）

- **编译 + 烧录**：仓库根目录 **`make.sh`**。
- **GH3018 串口自动化**：**`scripts/gh3018_full_pipeline.sh`**（内部调用 **`sifli_serial_batch.py`**）。
- 串口参数可通过 **`SIFLI_SERIAL_PORT`**、**`SIFLI_SERIAL_BAUD`** 覆盖。

详细命令与参数说明见：**`.cursor/skills/sifli-sf32-watch-sdk/SKILL.md`**。

---

## 7. 经验小结

1. **`__GET_RAWDATA_WITHOUT_ALGO_HANDLE`** 不仅是「功能开关」，会改变 **心率数据流经 Demo 还是 Hook**；改宏后必须同步检查 **`gh3018_set_hr`** 的挂载点。
2. **静态池大小** 宁可略大于表算值并配合 **单次日志 + 停线程**，也不要 **return 后继续用坏池**。
3. **自动化串口** 时避免默认清空 RX，并给烧录后 USB **足够的 settle 时间**。

---

## 8. 相关源文件索引

| 主题 | 路径 |
|------|------|
| RAWDATA / algo handle 宏 | `customer/peripherals/sensor/gh3018/gh30x_example_config.h` |
| 池大小最终宏 | `customer/peripherals/sensor/gh30x_algo_demo/common/inc/gh30x_demo_algo_memory.h` |
| 静态池与 PoolIsNotEnough | `customer/peripherals/sensor/gh30x_algo_demo/common/src/gh30x_demo_algo_memory.c` |
| HR Demo 算法 | `customer/peripherals/sensor/gh30x_algo_demo/call/src/gh30x_demo_algo_call_hr.c` |
| HR 结果上报（同步 `gh3018_set_hr`） | `customer/peripherals/sensor/gh30x_algo_demo/common/src/gh30x_demo_algo_hook.c` |
| Hook 路径 HR | `customer/peripherals/sensor/gh3018/gh30x_example_hook.c` |
| 应用缓存 get/set | `customer/peripherals/sensor/gh3018/gh30x_example_port.c`（`gh3018_get_hr` / `gh3018_set_hr`） |
| Shell `hbd` | `customer/peripherals/sensor/gh3018/gh3018.c` |

---

*文档随代码演进可能需更新；以仓库当前实现为准。*
