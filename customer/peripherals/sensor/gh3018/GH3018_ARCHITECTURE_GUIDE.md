# GH3018 驱动与算法库架构指南

本文档详细介绍 GH3018 PPG 心率传感器驱动库和算法库的架构、数据流和关键机制，帮助快速理解和调试该模块。

## 1. 整体架构

### 1.1 分层架构

```
┌─────────────────────────────────────┐
│         应用层 (heart_rate.c)        │  ← GUI 应用，显示心率/血氧数据
├─────────────────────────────────────┤
│         通知层 (gh30x_hr_ui_notify)  │  ← UI 通知机制，线程安全
├─────────────────────────────────────┤
│         算法层 (gh30x_example_hook)  │  ← 数据处理回调，算法调用
├─────────────────────────────────────┤
│         处理层 (gh30x_example_process)│ ← 中断消息处理，状态机
├─────────────────────────────────────┤
│         控制层 (gh30x_example_ctrl)   │ ← 寄存器操作，命令发送
├─────────────────────────────────────┤
│         端口层 (gh30x_example_port)   │ ← 硬件抽象，I2C/GPIO/中断
├─────────────────────────────────────┤
│         硬件层 (GH3018 芯片)         │ ← PPG 传感器，FIFO，中断
└─────────────────────────────────────┘
```

### 1.2 关键文件职责

| 文件 | 职责 | 关键函数/数据 |
|------|------|--------------|
| `gh30x_example_port.c` | 硬件抽象层 | I2C 读写、GPIO 控制、中断处理、传感器任务 |
| `gh30x_example_ctrl.c` | 控制层 | `HBD_TryToWakeUp()`、`HBD_I2cReadReg()`、`HBD_I2cWriteReg()` |
| `gh30x_example_process.c` | 处理层 | `gh30x_int_msg_handler()`、FIFO 事件处理、状态机 |
| `gh30x_example_hook.c` | 算法回调 | NADT 结果处理、心率/血氧算法调用 |
| `gh30x_example_reg_array.c` | 配置数组 | 寄存器配置表（HR、SPO2、ADT 模式） |
| `gh30x_example_config.h` | 配置定义 | 功能使能、通道数、采样率、FIFO 阈值 |
| `gh3018.c` | 上层接口 | `init_gh3018_sensor()`、`open_gh3018()`、API 互斥锁 |

## 2. 数据流

### 2.1 中断驱动的数据流

```
GH3018 芯片产生中断 (FIFO 水位/新数据/佩戴检测)
    ↓
GPIO 中断触发 (上升沿)
    ↓
gh30x_int_handle() [ISR]
    ├── g_uchNewIntFlag++          // 标记有新中断
    └── rt_sem_release()           // 释放信号量
    ↓
gh30x_sensor_task() [线程]
    ├── rt_sem_take()              // 等待信号量
    ├── gh30x_api_lock()           // 获取互斥锁
    ├── hal_gh30x_int_handler_bottom_half()
    │   └── gh30x_int_msg_handler()
    │       ├── HBD_GetIntStatus()           // 读取中断状态
    │       ├── 事件分发：
    │       │   ├── INT_STATUS_FIFO_WATERMARK → gh30x_fifo_evt_handler()
    │       │   ├── INT_STATUS_NEW_DATA → gh30x_new_data_evt_handler()
    │       │   ├── INT_STATUS_WEAR_DETECTED → gh30x_wear_evt_handler()
    │       │   ├── INT_STATUS_UNWEAR_DETECTED → gh30x_unwear_evt_handler()
    │       │   ├── INT_STATUS_CHIP_RESET → gh30x_reset_evt_handler()
    │       │   └── INT_STATUS_FIFO_FULL → gh30x_fifo_full_evt_handler()
    │       └── g_uchNewIntFlag = 0          // 清除中断标志
    ├── gh30x_api_unlock()         // 释放互斥锁
    └── rt_pin_irq_enable()       // 重新使能中断
```

### 2.2 FIFO 数据处理流

```
gh30x_fifo_evt_handler()
    ↓
Gh30xGetFifoRawdata()
    ├── HBD_I2cReadRawdataReg()    // 读取 FIFO 原始数据
    │   └── HBD_TryToWakeUp()      // 唤醒检查（关键！）
    └── 数据存入 g_UNGh30xRawdataBuf
    ↓
gh30x_algo_calculate()             // 算法计算
    ├── 通道数据提取
    ├── G-sensor 数据同步
    └── 调用算法回调 (gh30x_algo_calculate_hook)
        ↓
gh30x_algo_calculate_hook()        // 算法回调
    ├── 心率算法 (GH30xHrAlgoExe)
    ├── 血氧算法 (GH30xSpO2AlgoExe)
    ├── NADT 算法 (GH30xSoftAdtAlgoExe)
    └── 结果存储到 STGh30xAlgoResult
        ↓
gh3011_algo_calculate_hook()       // 上层回调
    ├── 心率值 → gh30x_hr_ui_notify_hr()
    ├── 血氧值 → gh30x_hr_ui_notify_spo2()
    └── 佩戴状态 → gh30x_hr_ui_notify_wear()
```

## 3. 关键数据结构

### 3.1 STGh30xFrameInfo - 帧信息结构

```c
typedef struct {
    GU32 *punFrameCnt;              // 帧计数器指针
    GU32 *punRawdata;               // 原始 PPG 数据指针
    GS16 *pusGsensordata;           // G-sensor 数据 (x, y, z)
    GU8 *pchDrvCurrentCode;         // LED 驱动电流代码
    STGh30xFunctionInfo *pstFunctionInfo;  // 功能信息
    GU8 *puchChnlMap;               // 通道映射
    STGh30xAlgoResult *pstAlgoResult;      // 算法结果
    GU32 unFunctionID;              // 功能 ID (HR/SPO2/ADT 等)
    GU8 uchFuntionChnlLimit;        // 通道数限制
} STGh30xFrameInfo;
```

### 3.2 STGh30xAlgoResult - 算法结果结构

```c
typedef struct {
    GS32 snResult[16];              // 算法结果数组
    GU8 uchUpdateFlag;              // 更新标志
    // ... 其他字段
} STGh30xAlgoResult;
```

**snResult 数组含义**（根据 __GET_RAWDATA_WITHOUT_ALGO_HANDLE=1）：
- `snResult[0]`: NADT 佩戴状态 (0=无更新, 1=佩戴, 2=未佩戴)
- `snResult[1]`: NADT 置信度
- `snResult[2]`: 心率值 (BPM)
- `snResult[3]`: 血氧值 (SpO2%)
- `snResult[4-7]`: HRV 数据 (RR 间期)

### 3.3 功能 ID 定义

```c
#define GH30X_FUNCTION_ADT          (0x01)  // 佩戴检测
#define GH30X_FUNCTION_HR           (0x02)  // 心率
#define GH30X_FUNCTION_HRV          (0x04)  // 心率变异性
#define GH30X_FUNCTION_SPO2         (0x08)  // 血氧
#define GH30X_FUNCTION_SOFT_ADT     (0x10)  // 软件佩戴检测 (NADT)
#define GH30X_FUNCTION_PROTOCOL     (0x20)  // 协议
```

## 4. 状态机与命令模式

### 4.1 芯片命令状态

```c
// 命令定义
#define HBD_CMD_SLEEP               (0x00)  // 睡眠
#define HBD_CMD_IDLE                (0x01)  // 空闲
#define HBD_CMD_SCAN                (0x02)  // 扫描（测量中）
#define HBD_CMD_RESUME              (0x03)  // 恢复

// 关键全局变量
GU8 g_uchGh30xLastCmd;      // 最后发送的命令
volatile GU8 g_uchNewIntFlag; // 中断标志 (0=无中断, >0=有中断)
```

### 4.2 HBD_TryToWakeUp() 机制

这是驱动中最关键的保护机制，防止在芯片睡眠时访问寄存器：

```c
void HBD_TryToWakeUp(void)
{
    if (HBD_CMD_IDLE == g_uchGh30xLastCmd)
    {
        // 芯片已唤醒，安全访问
    }
    else if ((HBD_CMD_SCAN == g_uchGh30xLastCmd) || 
             (HBD_CMD_RESUME == g_uchGh30xLastCmd))
    {
        if (0 == g_uchNewIntFlag)
        {
            // 致命错误：芯片在扫描/恢复模式，但无中断标志
            // 说明 MCU 在芯片睡眠期间访问寄存器
            EXAMPLE_DEBUG_LOG_L1("fatal error: mcu is accessing register/fifo at GH30X sleep period!");
            while(1);  // 死锁
        }
    }
    else
    {
        // 其他状态，发送 IDLE 命令唤醒芯片
        HBD_I2cSendCmd(HBD_CMD_IDLE);
    }
}
```

**调用时机**：每次寄存器读写前都会调用，包括：
- `HBD_I2cReadReg()`
- `HBD_I2cWriteReg()`
- `HBD_I2cReadRawdataReg()` (读取 FIFO)

### 4.3 g_uchNewIntFlag 生命周期

```
1. 初始状态: g_uchNewIntFlag = 0
2. 中断触发: g_uchNewIntFlag++ (在 ISR 中)
3. 消息处理开始: 检查 g_uchNewIntFlag != 0
4. 读取中断状态: HBD_GetIntStatus() (会访问寄存器)
5. 事件处理: gh30x_fifo_evt_handler() 等 (访问寄存器)
6. 处理完成: g_uchNewIntFlag = 0 (必须在所有寄存器访问后！)
```

**关键点**：`g_uchNewIntFlag` 必须在所有寄存器访问完成后才能清零，否则会导致 `HBD_TryToWakeUp()` 误判。

## 5. 配置加载机制

### 5.1 配置数组结构

```c
typedef struct {
    GU16 usRegAddr;    // 寄存器地址
    GU16 usRegValue;   // 寄存器值
} ST_REGISTER;

// 示例：心率模式配置
const ST_REGISTER hb_reg_config_array[] = {
    {0x0002, 0x132a}, {0x000C, 0x09f0}, ...
    {0x0118, 0x32a0}, {0x011A, 0x00a0}, ...  // LED 电流配置
};
```

### 5.2 配置加载流程

```c
GS8 gh30x_Load_new_config(const ST_REGISTER *config_ptr, uint16_t len)
{
    for (retry = 0; retry < __RETRY_MAX_CNT_CONFIG__; retry++)
    {
        if (HBD_LoadNewRegConfigArr(config_ptr, len) == HBD_RET_OK)
        {
            return HBD_RET_OK;
        }
    }
    return HBD_RET_COMM_ERROR;
}

// HBD_LoadNewRegConfigArr 内部：
// 1. 逐个写入寄存器
// 2. 回读验证
// 3. 失败时返回错误码
```

### 5.3 配置验证失败处理

```c
// 在 Gh30xDemoSampleStart() 中
GS8 cfg_ret = gh30x_Load_new_config(pstBaseArray, usBaseArrayLen);
if (cfg_ret != HBD_RET_OK)
{
    EXAMPLE_DEBUG_LOG_L1("[Gh30xDemoSampleStart] config load FAILED (%d), abort sample start\r\n", (int)cfg_ret);
    return;  // 配置失败，不启动采样
}
```

## 6. 传感器任务与线程模型

### 6.1 线程结构

```c
// 传感器任务线程
static void gh30x_sensor_task(void *params)
{
    while (1)
    {
        rt_sem_take(&gh3018_int_sem, RT_WAITING_FOREVER);  // 等待中断
        gh30x_api_lock();                                   // 获取锁
        hal_gh30x_int_handler_bottom_half();                // 处理中断
        gh30x_api_unlock();                                 // 释放锁
        rt_pin_irq_enable(PPG_INT_PIN, 1);                 // 重新使能中断
    }
}

// 线程配置
#define THREAD_STACK_SIZE (16 * 1024)  // 16KB 栈，算法需要较大空间
#define THREAD_PRIORITY 7
```

### 6.2 同步机制

- **信号量**：`gh3018_int_sem` - ISR 释放，任务等待
- **互斥锁**：`api_lock` - 保护 API 调用的线程安全
- **中断使能**：处理完成后重新使能 GPIO 中断

## 7. UI 通知机制

### 7.1 线程安全通知

```c
// gh30x_hr_ui_notify.c
// 使用消息队列实现线程安全的 UI 更新

// 心率通知
void gh30x_hr_ui_notify_hr(uint32_t hr);

// 血氧通知
void gh30x_hr_ui_notify_spo2(uint32_t spo2);

// 佩戴状态通知
void gh30x_hr_ui_notify_wear(uint8_t wear_status);

// 重置通知状态
void gh30x_hr_ui_notify_reset();
```

### 7.2 消息队列

```c
// 消息类型
#define GH30X_HR_MSG_HR     1
#define GH30X_HR_MSG_SPO2   2
#define GH30X_HR_MSG_WEAR   3

// 消息结构
typedef struct {
    uint32_t type;
    uint32_t value;
} gh30x_hr_msg_t;
```

## 8. 调试与诊断

### 8.1 调试日志系统总览

GH3018 驱动和算法库有 **四层独立的日志控制**，需要分别开关：

| 层级 | 控制宏 | 文件位置 | 作用域 |
|------|--------|----------|--------|
| 驱动框架日志 | `__EXAMPLE_DEBUG_LOG_LVL__` | `gh30x_example_config.h` | 全部 `EXAMPLE_DEBUG_LOG_L1/L2` 宏调用 |
| RT-Thread 驱动日志 | `DRV_DEBUG` | `gh3018.c`, `gh30x_example_port.c` | RT-Thread `LOG_D` 宏 |
| 内联调试块 | `#if 1 /* [DBG_...] */` | 各 `.c` 文件 | 每个调试块独立控制 |
| 算法库日志 | `__ALGO_LOG_CONFIG__` | `gh30x_demo_algo_config.h` | 算法库内部日志 |

详细开关方法见 **10.3 日志开关速查表**。

### 8.2 关键调试点

1. **中断标志**：`g_uchNewIntFlag` 的值
2. **芯片命令**：`g_uchGh30xLastCmd` 的状态
3. **配置加载**：验证寄存器回读值
4. **FIFO 状态**：水位、溢出检测
5. **算法结果**：`snResult[]` 数组内容

### 8.3 MSH 调试命令

```bash
hbd -open    # 初始化 GH3018 模块
hbd -hb      # 启动心率测量模式
hbd -peek    # 读取缓存的 BPM 值
hbd -stop    # 停止模块
```

## 9. 常见问题与解决思路

### 9.1 配置加载失败

**现象**：`VERIFY FAIL: reg=0xXXXX expected=0xYYYY actual=0xZZZZ`

**原因**：
- 芯片硬件特性（如 0x011A 寄存器）
- I2C 通信问题
- 芯片未正确复位

**解决**：
- 检查芯片数据手册，确认寄存器行为
- 修改配置数组中的期望值
- 增加重试次数或复位逻辑

### 9.2 致命错误：睡眠期间访问

**现象**：`fatal error: mcu is accessing register/fifo at GH30X sleep period!`

**原因**：
- `g_uchNewIntFlag` 过早清零
- 在 `HBD_TryToWakeUp()` 检查时标志为 0
- 但芯片仍在扫描/恢复模式

**解决**：
- 确保 `g_uchNewIntFlag` 在所有寄存器访问完成后才清零
- 检查事件处理函数中的寄存器访问顺序

### 9.3 无中断/数据

**现象**：传感器启动后无数据

**检查**：
1. GPIO 中断配置（上升沿触发）
2. 中断使能状态
3. 芯片电源和复位
4. I2C 通信是否正常
5. 配置是否正确加载

### 9.4 算法内存错误

**现象**：`MEMORY ERROR` 或算法崩溃

**原因**：
- 栈空间不足（算法需要较大栈）
- 算法库内存池溢出

**解决**：
- 增加线程栈大小（建议 16KB+）
- 检查算法库内存配置

## 10. 扩展与定制

### 10.1 添加新功能

1. 在 `gh30x_example_config.h` 使能功能宏
2. 在 `gh30x_example_reg_array.c` 添加配置数组
3. 在 `gh30x_example_process.c` 添加事件处理
4. 在 `gh30x_example_hook.c` 添加算法回调

### 10.2 修改采样参数

```c
// 修改采样率
#define __GET_RAWDATA_SAMPLE_RATE__  (25)  // Hz

// 修改 FIFO 阈值
#define __GET_RAWDATA_FIFO_THR_CNT_CONFIG__ (25)  // 样本数

// 修改缓冲区大小
#define GH30X_RAWDATA_FIFO_BUF_SAMPLE_POINT_NUM_MAX (60)
```

### 10.3 日志开关速查表

> **当前状态（2026-05-30）：全部日志已关闭。** 如需调试，按以下方法逐层开启。

#### 第1层：驱动框架日志（`EXAMPLE_DEBUG_LOG_L1/L2`）

控制约 **173 处**日志调用，是最大的日志源。

```c
// 文件：customer/peripherals/sensor/gh3018/gh30x_example_config.h
// 第 118-125 行

/// log debug lvl: <0=> off , <1=> normal info ,  <2=> with data info
#ifdef GOODIX_DEMO_PLANFORM
#define __EXAMPLE_DEBUG_LOG_LVL__           (0)   // ← 改为 (1) 或 (2) 开启
#else
#define __EXAMPLE_DEBUG_LOG_LVL__           (0)   // ← 改为 (1) 或 (2) 开启
#endif
```

| 值 | 效果 |
|----|------|
| `(0)` | 关闭所有 `EXAMPLE_DEBUG_LOG_L1/L2` 调用（宏展开为空） |
| `(1)` | 开启 `L1` 级别（错误/状态信息） |
| `(2)` | 开启 `L1` + `L2` 级别（含详细数据） |

#### 第2层：RT-Thread 驱动日志（`LOG_D`）

控制 RT-Thread 驱动框架的调试输出。

```c
// 文件：customer/peripherals/sensor/gh3018/gh3018.c     — 第 8 行附近
// 文件：customer/peripherals/sensor/gh3018/gh30x_example_port.c — 第 13 行附近

// #define DRV_DEBUG          // ← 取消注释开启
#define DRV_DEBUG              // ← 取消注释后的效果
```

| 状态 | 效果 |
|------|------|
| 注释 `// #define DRV_DEBUG` | 关闭 `LOG_D` 输出 |
| 取消注释 `#define DRV_DEBUG` | 开启 `LOG_D` 输出 |

#### 第3层：内联调试块（`#if 1/0`）

每个调试块独立控制，修改 `#if 0` ↔ `#if 1` 即可。**热路径中的调试块（如 FIFO 处理）默认关闭，避免溢出。**

| 标签 | 文件 | 行号(约) | 内容 |
|------|------|----------|------|
| `[DBG_LED]` | `gh30x_example_process.c` | 289 | LED 电流/通道信息 |
| `[DBG_MOD]` | `gh30x_example_process.c` | 469 | 模块启动参数 |
| `[DBG_CHNL]` | `gh30x_example_process.c` | 566 | 通道映射配置 |
| `[DBG_FRAME]` | `gh30x_example_process.c` | 1676 | HR 帧数据 |
| `[DBG_FRAME_SADT]` | `gh30x_example_process.c` | 1705 | SADT 帧数据 |
| `[DBG_FIFO]` | `gh30x_example_process.c` | 1877 | FIFO 读取统计 |
| `[DBG_FIX]` | `gh30x_example_process.c` | 1903 | NeedReadCnt=0 修复 |
| `[DBG_BITF]` | `gh30x_example_ctrl.c` | 1341 | 寄存器位域读取 |
| `[DBG_CHNL]` | `gh30x_example_ctrl.c` | 2314 | 通道信息获取 |
| `[DBG_HOOK]` | `gh30x_example_hook.c` | 83 | 算法计算入口 |
| `[DBG_HR]` | `gh30x_example_hook.c` | 102 | 心率算法结果 |
| `[DBG_NADT_HOOK]` | `gh30x_example_hook.c` | 177 | NADT 回调详细数据 |
| `[DBG_NADT]` | `gh30x_example_hook.c` | 221 | NADT 佩戴状态变化 |
| `[DBG_ALGO]` | `gh30x_example_hook.c` | 583 | 算法输入数据 |
| `[DBG_NOTIFY]` | `gh30x_hr_ui_notify.c` | 10-71 | UI 通知事件（7处） |

**示例**（开启 `[DBG_HR]`）：
```c
// gh30x_example_hook.c
#if 1 /* [DBG_HR] */       // ← 改 0 为 1
    rt_kprintf("[DBG_HR] AlgoResult: UpdateFlag=%d BPM=%d ...\r\n", ...);
#endif
```

#### 第4层：算法库日志

```c
// 文件：customer/peripherals/sensor/gh30x_algo_demo/common/inc/gh30x_demo_algo_config.h
// 第 46 行附近

#define __ALGO_LOG_CONFIG__                    (0)   // ← 改为 (1) 开启
```

| 值 | 效果 |
|----|------|
| `(0)` | 关闭算法库内部日志 |
| `(1)` | 开启算法库内部日志 |

#### 快速操作指南

**全部关闭**（生产环境，当前状态）：
```
__EXAMPLE_DEBUG_LOG_LVL__ = (0)
DRV_DEBUG 注释掉
所有 #if 1 /* [DBG_...] */ 改为 #if 0
__ALGO_LOG_CONFIG__ = (0)
```

**全部开启**（开发调试）：
```
__EXAMPLE_DEBUG_LOG_LVL__ = (2)
DRV_DEBUG 取消注释
需要的 #if 0 /* [DBG_...] */ 改为 #if 1
__ALGO_LOG_CONFIG__ = (1)
```

**仅开启特定模块**（推荐调试方式）：
- 只改 `__EXAMPLE_DEBUG_LOG_LVL__` 控制框架日志
- 只改对应的 `#if 0/1` 控制特定调试块
- 避免在热路径（`[DBG_FIFO]`、`[DBG_FRAME]`）开启，会导致 FIFO 溢出

## 11. 注意事项

1. **栈空间**：算法回调需要较大栈空间，线程栈建议 16KB+
2. **中断时序**：`g_uchNewIntFlag` 清零时机至关重要
3. **寄存器访问**：必须通过 `HBD_TryToWakeUp()` 保护
4. **线程安全**：所有 API 调用需要通过互斥锁保护
5. **配置验证**：加载配置后必须验证回读值
6. **电源管理**：芯片睡眠/唤醒状态需要正确管理

---

*本文档基于 GH3018 SDK v0.6.2 和 HBD 控制库 v0.7.8.6 编写。*