# 指南针校准：开发者手册（术语 + 算法 + 实现）

本文仅面向 **固件 / 算法开发者**，说明 SF32 手表工程中指南针 **磁力计校准** 所涉及的概念、数学模型、本仓库的具体实现与调参入口。不涉及最终用户操作说明。

---

## 目录

1. [术语表（专有名词）](#1-术语表专有名词)  
2. [问题建模：理想传感器 vs 实际误差](#2-问题建模理想传感器-vs-实际误差)  
3. [本工程采用的简化：2D + 倾斜补偿](#3-本工程采用的简化2d--倾斜补偿)  
4. [倾斜补偿算法（与 `compass.c` 一致）](#4-倾斜补偿算法与-compassc-一致)  
5. [校准数据采集与运行时结构](#5-校准数据采集与运行时结构)  
6. [覆盖率（coverage）与位图设计](#6-覆盖率coverage与位图设计)  
7. [finalize：协方差、特征分解、椭圆白化矩阵](#7-finalize协方差特征分解椭圆白化矩阵)  
8. [硬铁 bias 与跨度检查](#8-硬铁-bias-与跨度检查)  
9. [质量分 quality 与有效性校验](#9-质量分-quality-与有效性校验)  
10. [在线应用 `compass_calib_apply` 与航向](#10-在线应用-compass_calib_apply-与航向)  
11. [应用层状态机：`compass.c`](#11-应用层状态机compassc)  
12. [持久化：`share_prefs` / FlashDB / FAL](#12-持久化share_prefs--flashdb--fal)  
13. [源码索引与结构体字段说明](#13-源码索引与结构体字段说明)  
14. [编译期宏一览](#14-编译期宏一览)  
15. [调试与常见工程问题](#15-调试与常见工程问题)  

---

## 1. 术语表（专有名词）

### 1.1 磁场与单位

| 名词 | 含义 |
|------|------|
| **地磁场（Geomagnetic field）** | 地球产生的矢量场，地表附近量级约 **25–65 µT**（随纬度、高度变化）。指南针利用其在水平面内的方向指北。 |
| **磁力计（Magnetometer）** | 测量磁感应强度三轴分量的 MEMS 传感器。本工程设备名 `mag_mmc56x3`，对应芯片常为 MMC56x3 系列。 |
| **µT / uT（microtesla）** | 磁感应强度单位，**微特斯拉**。代码里注释写 `x_uT` 表示该浮点数按 **微特斯拉** 理解（与驱动上报标定一致）。 |
| **硬铁误差（Hard-iron）** | 传感器附近 **恒定** 的附加磁场（永磁体、直流电流回路等），效果是把测量点云 **整体平移**。可用 **偏置向量 \(\mathbf{b}\)** 建模。 |
| **软铁误差（Soft-iron）** | 可磁化材料使场线弯曲，效果接近对磁场做 **线性变换**（各轴增益不同 + 轴间耦合），点云从球/圆 **仿射成椭球/椭圆**。 |
| **航向 / 磁航向（Heading / Magnetic heading）** | 在水平面内，相对 **磁北** 的方位角，本工程 UI 采用 **北为 0°、顺时针增加**（与航空/航海常用定义一致，需注意与数学极角区别）。 |

### 1.2 姿态与坐标

| 名词 | 含义 |
|------|------|
| **机体坐标系（Body frame）** | 固连在手表上的坐标系：通常 **x/y/z** 与 PCB 或芯片手册定义一致。磁力计、加速度计输出都在此系下。 |
| **水平面 / 切平面** | 当地重力 **垂直** 的平面。静止时加速度计主要测重力，可估计姿态，把磁矢量 **投影** 到水平面，得到用于指北的 **二维向量**。 |
| **Roll（横滚）** | 绕机体 **x 轴** 的旋转角；本代码用 `atan2(ay, az)` 与加速度分量定义（见 §4）。 |
| **Pitch（俯仰）** | 绕机体 **y 轴** 的旋转角；本代码用 `atan2(-ax, sqrt(ay²+az²))` 定义（见 §4）。 |
| **倾斜补偿（Tilt compensation）** | 利用加速度估计 roll/pitch，将 **三轴磁力** 换算到 **近似水平面** 上的两分量，消除俯仰横滚对航向的影响。 |
| **Yaw（航向角）** | 绕 **竖直轴**（通常天向）的转角；在水平面磁向量已知时，由 `atan2` 类函数推出。本工程在 2D 补偿后直接对 \((v_x,v_y)\) 算航向。 |

### 1.3 数学与信号处理

| 名词 | 含义 |
|------|------|
| **`atan2(y, x)`** | 四象限反正切，返回 \((x,y)\) 相对 x 轴正方向的角度（弧度），避免 `atan(y/x)` 的象限歧义与除零。 |
| **协方差矩阵（Covariance）** | 对随机向量，描述各分量 **波动** 与 **相关**。二维对称矩阵 \(\begin{bmatrix}\sigma_{xx}&\sigma_{xy}\\\sigma_{xy}&\sigma_{yy}\end{bmatrix}\)。本工程用样本 **二阶矩 − 均值外积** 估计。 |
| **特征值 / 特征向量（Eigenvalue / Eigenvector）** | 对称矩阵可正交对角化；特征向量方向是 **主轴方向**，特征值是在该方向上的 **方差**。二维椭圆点云的长轴/短轴方向即协方差矩阵的特征向量。 |
| **椭圆白化 / 归一化（Elliptic whitening）** | 对去中心后的数据做线性变换，使 **协方差接近各向同性**（圆）。本工程在 2D 上用 **沿主轴不同缩放** 再旋回原坐标，得到一个 **对称 2×2 矩阵 \(M\)**。 |
| **AABB（Axis-Aligned Bounding Box）** | 轴对齐包围盒；此处为 \([min_x,max_x]\times[min_y,max_y]\)，用于 **跨度** 与 **硬铁中心近似**。 |
| **Bias（偏置）** | 校准输出的 \((b_x,b_y)\)，从测量中减去以消除硬铁平移。本实现取 **min/max 中点** 而非样本均值（见 §8）。 |

### 1.4 本工程特有量

| 名词 | 含义 |
|------|------|
| **覆盖率（coverage）** | 将平面按 **每 10° 一扇区** 共 36 扇区离散化，统计 **去中心后** 样本方向落入了多少个不同扇区；占比为 `命中扇区数 / 36`。用于判断用户是否 **在水平面内扫过足够多方向**。 |
| **quality（质量分）** | `finalize` 时由 **覆盖率、椭圆扁率、XY 跨度** 加权得到的标量 \([0,1]\)，用于拒绝劣质校准；持久化与 `compass_calib_params_valid` 要求 **≥ 0.35**（可编译覆盖）。 |
| **Live 校准** | 无有效 Flash 参数时，后台 `s_runtime_bg` 静默收集样本，满足门槛后只在 **RAM** 中生成 `s_calib_live`，**掉电丢失**。 |
| **Saved 校准** | 从 Flash 读出或用户校准成功后写入的 `s_calib_saved`。 |

### 1.5 系统与存储

| 名词 | 含义 |
|------|------|
| **RT-Thread `rt_sensor_data`** | 统一传感器数据帧；磁力与加速度字段通过 `rt_device_read` 填入 `struct rt_sensor_data`。 |
| **LVGL 角度单位** | `lv_img_set_angle` 使用 **0.1 度** 为单位（代码里 `deg * 10` 四舍五入再取模 3600）。 |
| **share_prefs** | 本 SDK 中间件，提供 **键值 / 二进制块** 的持久化抽象；指南针用 **私有命名空间 + 定长块** 存 `compass_calib_params_t`。 |
| **FlashDB（FDB）** | 嵌入式 KV 数据库；`share_prefs_flashdb` 后端在名为 **`prefdb`** 的 **FAL 分区** 上初始化 KVDB。 |
| **FAL（Flash Abstraction Layer）** | 将 **逻辑分区名**（如 `"prefdb"`）映射到 **物理 Flash 偏移与长度**；`fal_partition_find` 找不到分区则 DB 初始化失败。 |
| **Magic / Version** | 结构体头部 `magic = 'CMCB'`、`version` 固定值，用于防止读出未初始化 Flash 或旧版布局被误解析。 |

---

## 2. 问题建模：理想传感器 vs 实际误差

### 2.1 理想三轴磁力计

在机体坐标系下，若只有地磁场 \(\mathbf{h}\)，测量应为：

\[
\mathbf{m}_{\text{ideal}} = \mathbf{h}.
\]

用户旋转设备时，\(\mathbf{h}\) 在传感器坐标系下方向变化，**模长近似恒定**，端点轨迹在以原点为中心的 **球面** 上。

### 2.2 硬铁 + 软铁（经典仿射模型）

更一般的 **线性误差模型** 写为：

\[
\mathbf{m}_{\text{meas}} = A\,\mathbf{h} + \mathbf{b},
\]

- \(\mathbf{b}\)：硬铁，**常向量**。  
- \(A\)：**可逆 3×3**（软铁 + 比例因子）；把球面变成 **椭球**，中心平移到 \(\mathbf{b}\)。

校准目标：由大量 \(\mathbf{m}_{\text{meas}}\) 样本估计 \(\mathbf{b}\) 与 \(A\)（或等价地估计 **椭球参数**），再求逆映射恢复 \(\mathbf{h}\) 方向。

### 2.3 本工程只做的子问题

手表固件选择：

- 只用 **水平面内两维** \((m_x', m_y')\)（经倾斜补偿得到，见 §3–§4）。  
- 误差模型退化为 **2D 仿射**：椭圆 + 平移，用 **\(\mathbf{b}\in\mathbb{R}^2\)** 与 **2×2 对称矩阵 \(M\)**（白化）近似。

**未建模**：三轴椭球完整拟合、温漂、运动加速度对姿态估计的干扰、长期软磁滞等。

---

## 3. 本工程采用的简化：2D + 倾斜补偿

**原因**：全 3D 椭球拟合计算与存储更大；手表 UI 主要关心 **水平指北**。  
**流程**：

1. 读 \((m_x,m_y,m_z)\) 与加速度 \((a_x,a_y,a_z)\)。  
2. 用加速度 **归一化重力方向** 估 roll/pitch，把磁矢量旋转/投影到 **水平系**，得到 \((m_x',m_y')\)（代码变量 `mx_use`, `my_use`）。  
3. 在 \((m_x',m_y')\) 平面上做 **采集 → 协方差椭圆 → bias + 矩阵 \(M\)**。  
4. 实时：\((v_x,v_y) = M\bigl((m_x',m_y') - \mathbf{b}\bigr)\)，再算航向。

**代价**：大加速度（剧烈运动）时重力方向不准，倾斜补偿会差；磁力计附近强干扰仍可能使 2D 模型失效。

---

## 4. 倾斜补偿算法（与 `compass.c` 一致）

实现函数：`compute_tilt_compensated_xy`（`compass.c`）。

### 4.1 加速度归一化

\[
\hat{a} = \frac{(a_x,a_y,a_z)}{\lVert (a_x,a_y,a_z)\rVert}.
\]

若模长过小或非有限，返回失败，**不进行补偿**（后续仍用原始 \(m_x,m_y\)，航向在倾斜时可能漂）。

### 4.2 Roll / Pitch

与常见「重力在机体前-右-下」约定一致的一种写法：

\[
\text{roll} = \operatorname{atan2}(a_y, a_z),\quad
\text{pitch} = \operatorname{atan2}\bigl(-a_x,\sqrt{a_y^2+a_z^2}\bigr).
\]

### 4.3 旋转到水平磁分量

记 \(s_r=\sin(\text{roll}),\, c_r=\cos(\text{roll}),\, s_p=\sin(\text{pitch}),\, c_p=\cos(\text{pitch})\)。

代码给出：

\[
\begin{aligned}
h_x &= m_x c_p + m_z s_p,\\
h_y &= m_x s_r s_p + m_y c_r - m_z s_r c_p.
\end{aligned}
\]

输出 \((h_x,h_y)\) 即用于校准与指北的 **等效水平面磁力分量**。  
可选输出 **tilt_deg** \(= \arccos(\mathrm{clamp}(\hat{a}_z,-1,1))\)，用于与重力夹角相关的诊断（近似「竖直倾斜程度」）。

---

## 5. 校准数据采集与运行时结构

结构体：`compass_calib_runtime_t`（`compass_calib.h`）。

每收到一对 \((x,y)\)（单位视为 uT），`compass_calib_runtime_add_sample` 做：

1. **初始化**：首样本设置 `min_* / max_*`。  
2. **更新 AABB**：扩展 `min_x,max_x,min_y,max_y`。  
3. **计数**：`sample_count++`。  
4. **累计矩**：  
   \(S_x=\sum x,\; S_y=\sum y,\; S_{xx}=\sum x^2,\; S_{xy}=\sum xy,\; S_{yy}=\sum y^2\)。  
5. **角度分箱（用于 coverage）**：  
   - 临时中心 \(c_x=\frac{min_x+max_x}{2},\; c_y=\frac{min_y+max_y}{2}\)。  
   - 向量 \((v_x,v_y)=(x-c_x,\,y-c_y)\)，若模平方 \(>10^{-4}\)，则  
     \(\theta=\mathrm{atan2}(v_y,v_x)\) 转度并归一化到 \([0,360)\)。  
   - `bin = floor(theta/10)`，限制在 \([0,35]\)，在 `angle_bins` 的对应 **bit** 置 1。

**为何用动态 min/max 中心而非固定原点分箱？**  
硬铁很大时，未去中心的 \((x,y)\) 可能整体偏离原点，`atan2(y,x)` 会 **挤在窄角度区间**，覆盖率 **假性偏低**。用 **当前包围盒中心** 近似硬铁中心，使分箱对「是否绕圈」更敏感。见 `compass_calib.c` 注释。

---

## 6. 覆盖率（coverage）与位图设计

- `angle_bins` 为 **64 bit**，只用低 **36 bit**，对应 **36 个 10° 扇区**（\(360/10\)）。  
- `compass_calib_runtime_coverage`：对 `angle_bins` 做 **popcount**（Brian Kernighan 风格循环），得到置位个数 `cnt`，返回 `cnt/36.f`。

**注意**：覆盖率是 **离散方向多样性** 指标，**不是** 统计置信度的严格度量；与样本数阈值一起使用，避免用户只在一个方向抖动。

---

## 7. finalize：协方差、特征分解、椭圆白化矩阵

函数：`compass_calib_runtime_finalize`（`compass_calib.c`）。

### 7.1 前置条件

- `sample_count >= COMPASS_CALIB_MIN_SAMPLES`（默认 **120**）。  
- `span_x = max_x-min_x \ge COMPASS_CALIB_MIN_SPAN_UT`（默认 **12 uT**），`span_y` 同理。  
否则返回失败：数据几乎没动或环境异常。

### 7.2 样本均值与协方差（总体式估计）

记 \(n=\) `sample_count`，

\[
\mu_x=\frac{S_x}{n},\quad \mu_y=\frac{S_y}{n},
\]

\[
C_{xx}=\frac{S_{xx}}{n}-\mu_x^2,\quad
C_{xy}=\frac{S_{xy}}{n}-\mu_x\mu_y,\quad
C_{yy}=\frac{S_{yy}}{n}-\mu_y^2.
\]

对称矩阵 \(C=\begin{bmatrix}C_{xx}&C_{xy}\\C_{xy}&C_{yy}\end{bmatrix}\) 为样本协方差（与除以 \(n\) 或 \(n-1\) 的细微差别在嵌入式里通常忽略）。

### 7.3 二维对称矩阵的特征值（解析式）

\[
\mathrm{trace}=C_{xx}+C_{yy},\quad
\det=C_{xx}C_{yy}-C_{xy}^2.
\]

判别式 \(\Delta=\mathrm{trace}^2-4\det\)，取 \(\sqrt{\max(\Delta,0)}\)，则

\[
\lambda_1=\frac{\mathrm{trace}+\sqrt{\Delta}}{2},\quad
\lambda_2=\frac{\mathrm{trace}-\sqrt{\Delta}}{2}.
\]

代码要求 \(\lambda_1,\lambda_2>10^{-4}\)，且 **保证 \(\lambda_1\ge\lambda_2\)**（若否则交换）。  
几何上：\(\lambda_1\) 对应 **长轴方向** 的方差更大。

### 7.4 属于 \(\lambda_1\) 的特征向量

取

\[
\mathbf{v}=(v_x,v_y)=(C_{xy},\;\lambda_1-C_{xx}),
\]

若 \(\lVert\mathbf{v}\rVert<10^{-6}\) 则退化为 \((1,0)\)，否则 **单位化**。  
（当 \(C_{xy}\approx0\) 且 \(C_{xx}\approx\lambda_1\) 时需数值稳定处理，代码已做退化分支。）

另一主轴为单位向量 \(\mathbf{w}=(-v_y,v_x)\)（与 \(\mathbf{v}\) 正交），对应 \(\lambda_2\)。

### 7.5 主轴缩放系数

令

\[
k=\sqrt{\frac{\lambda_1+\lambda_2}{2}},
\]

即两特征值 **算术平均** 再开方，作为目标 **各向同性方差尺度**。  
沿主轴缩放：

\[
s_1=\frac{k}{\sqrt{\lambda_1}},\quad s_2=\frac{k}{\sqrt{\lambda_2}}.
\]

在 \(\{\mathbf{v},\mathbf{w}\}\) 基下，变换为 \(\mathrm{diag}(s_1,s_2)\)。写回标准基：

\[
M = s_1\,\mathbf{v}\mathbf{v}^{\mathsf T} + s_2\,\mathbf{w}\mathbf{w}^{\mathsf T}.
\]

展开（\(\mathbf{w}=(-v_y,v_x)\)）即代码中的：

\[
\begin{aligned}
M_{00}&=s_1 v_x^2 + s_2 v_y^2,\\
M_{01}=M_{10}&=(s_1-s_2)v_x v_y,\\
M_{11}&=s_1 v_y^2 + s_2 v_x^2.
\end{aligned}
\]

**直观**：在去中心后的坐标里，点云近似椭圆；先在 **主轴坐标系** 把长轴压短、短轴拉长，使变成圆，再 **旋回** 到芯片 xy 轴方向，故 \(M\) **对称正定**（在良采样下）。

### 7.6 输出到 `compass_calib_params_t`

- `bias_x,bias_y`：见 §8。  
- `mat00..mat11`：即上述 \(M\)。  
- `quality, sample_count, magic, version`：见 §9。

---

## 8. 硬铁 bias 与跨度检查

**Bias** 取 AABB 中心：

\[
b_x=\frac{min_x+max_x}{2},\quad b_y=\frac{min_y+max_y}{2}.
\]

**而非** 用 \((\mu_x,\mu_y)\) 的原因（实现意图）：用户若 **只转了半圈**，样本均值会偏向弧的一侧；min/max 包围盒中心对 **椭圆圆心** 的近似在偏采样下 **更稳健**（仍非最优估计器，但计算极简）。

**跨度**：要求 \(max_x-min_x\) 与 \(max_y-min_y\) 均 ≥ `COMPASS_CALIB_MIN_SPAN_UT`，防止点云挤在噪声尺度内就通过。

---

## 9. 质量分 quality 与有效性校验

### 9.1 quality 公式（`compass_calib_runtime_finalize`）

\[
\begin{aligned}
q_{\text{cov}} &= \mathrm{clamp}\Bigl(\frac{\lambda_2}{\lambda_1},\,0,\,1\Bigr),\\
q_{\text{span}} &= \mathrm{clamp}\Bigl(\frac{\min(span_x,span_y)}{2\cdot \text{MIN\_SPAN}},\,0,\,1\Bigr),\\
q_{\text{cover}} &= \mathrm{clamp}(\text{coverage},\,0,\,1),\\
\text{quality} &= 0.40\,q_{\text{cover}} + 0.35\,q_{\text{cov}} + 0.25\,q_{\text{span}}.
\end{aligned}
\]

- **\(q_{\text{cov}}\)**：椭圆越扁（\(\lambda_2\ll\lambda_1\)）越小，表示 **软铁各向异性强或采样差**，不信任度升高。  
- **\(q_{\text{span}}\)**：鼓励足够大的磁变化幅度。  
- **\(q_{\text{cover}}\)**：鼓励方向扫全。

### 9.2 `compass_calib_params_valid`

通过需同时满足：

- `magic == COMPASS_CALIB_MAGIC`，`version == COMPASS_CALIB_VERSION`。  
- `sample_count >= COMPASS_CALIB_MIN_SAMPLES`（与 finalize 一致，默认 120）。  
- `bias` 与 `mat**` 均为有限浮点。  
- **`quality >= 0.35f`**（硬编码阈值）。

不满足时 `compass_calib_apply` **直通原值**（不做校准），持久化 load 也会判无效。

---

## 10. 在线应用 `compass_calib_apply` 与航向

### 10.1 应用校准

若参数有效：

\[
\begin{bmatrix}o_x\\o_y\end{bmatrix}
=
M
\left(
\begin{bmatrix}x\\y\end{bmatrix}
-
\begin{bmatrix}b_x\\b_y\end{bmatrix}
\right).
\]

无效则 \(o_x=x,\;o_y=y\)。

### 10.2 弱场门限（`compass.c`）

若 \(o_x^2+o_y^2 < \text{COMPASS\_MIN\_VEC\_NORM\_UT}^2\)（默认 **2 uT** 平方），认为 **场强过弱或干扰异常**，不更新航向并提示「磁干扰较强」。

### 10.3 航向角与 UI

`heading_from_horizontal_field(vx, vy)`：

- 先 \(\theta_{\text{plane}}=\mathrm{atan2}(v_y,v_x)\)（弧度转度）。  
- 再 `norm360(90 - theta_plane + HEADING_OFFSET + MAG_CHIP_MOUNT_ROTATE)`。

**含义**：把数学极角与 **「北零顺时针」** 约定对齐，并吸收 **芯片安装角** 与产品定义零点。

**平滑**：对角度做 **最短弧差** 插值（处理 360° 跳变），系数 `COMPASS_ANGLE_SMOOTH`（默认 0.25）。

**标签与指针分离**：`COMPASS_LABEL_MIRROR_EW` 可对 **显示方位字** 做东西镜像；指针单独加 `COMPASS_NEEDLE_IMG_OFFSET_DEG`。  
LVGL 角度：`ui_deg_to_lv_angle_01` 把度映射到 **0.1° 整数** 并模 3600。

---

## 11. 应用层状态机：`compass.c`

### 11.1 定时器 tick（50 ms）

- 读磁力计；失败则直接返回（可开 `COMPASS_DBG` 看连续失败）。  
- 倾斜补偿得 `mx_use, my_use`。

**分支 A — `s_calibrating == true`（用户校准模式）**

- 样本进入 `s_runtime_cal`。  
- UI 显示 `coverage*100%` 为进度。  
- 若 `sample_count >= COMPASS_AUTO_CAL_MIN_SAMPLES`（**220**）且 `coverage >= COMPASS_AUTO_CAL_MIN_COVERAGE`（**0.70**），调用 `calib_mode_finish(true)` 自动结束。  
- 用户也可点击按钮 `calib_mode_finish(false)`。

**分支 B — 非校准**

- 背景 `s_runtime_bg` 同样累加；若 **尚无 saved/live**，且达到与自动校准 **相同** 的样本数与覆盖率门槛，则 `finalize` 写入 `s_calib_live`。  
- `get_working_calib()`：**优先** 有效 `s_calib_saved`，否则有效 `s_calib_live`。  
- 若无校准：提示用户去校准。  
- 若有：apply → 弱场检查 → 航向平滑 → UI。

### 11.2 生命周期与传感器

- `on_start`：`compass_calib_store_load` → 开磁力计/加速度计 → 建 UI → 开 timer。  
- `on_pause` / `on_stop`：关 timer、关设备（若本 app 打开的会 close）。  
- 指针图使用 `app_cache_copy_alloc` 旋转缓存。

### 11.3 校准结束 `calib_mode_finish`

- `finalize` 成功：`s_calib_saved` 更新，`s_has_live=false`，`compass_calib_store_save`；根据返回值改提示文案。  
- 失败：FAIL 文案。  
- 最后 `compass_runtime_reset()` 清空背景运行时与部分 UI 状态。

---

## 12. 持久化：`share_prefs` / FlashDB / FAL

- **文件**：`compass_calib_store.c`。  
- **打开**：`share_prefs_open("compass", SHAREPREFS_MODE_PRIVATE)`。  
- **读写键**：`"calib_v1"`，值长度 `sizeof(compass_calib_params_t)`。  
- **依赖**：`CONFIG_BSP_SHARE_PREFS=y`；HCPU 上常见后端为 **FlashDB**，分区逻辑名 **`prefdb`**。  
- **板级要求**：`FAL_PART_TABLE` 中必须有 **`"prefdb"`** 项，且 `ptab.json` / 生成头文件中偏移、大小与 Flash 布局一致；否则 `share_prefs_open` 失败，串口 `[compass-store] ... failed`。

修改二进制布局时：**递增 `COMPASS_CALIB_VERSION`** 并改用新 key（如 `calib_v2`），避免旧数据被误读。

---

## 13. 源码索引与结构体字段说明

| 文件 | 职责 |
|------|------|
| `compass.c` | 传感器、倾斜补偿、UI、校准状态机、航向、内置应用注册 |
| `compass_calib.c` / `.h` | 运行时统计、覆盖率、finalize、apply、valid |
| `compass_calib_store.c` / `.h` | share_prefs 持久化 |
| `SConscript` | 编译集成 |

### `compass_calib_params_t`

| 字段 | 含义 |
|------|------|
| `magic`, `version` | 校验 |
| `bias_x`, `bias_y` | 硬铁，AABB 中心 |
| `mat00`…`mat11` | 对称 2×2 白化矩阵 \(M\) |
| `quality` | 综合质量分 |
| `sample_count` | finalize 时的样本数（元数据） |

### `compass_calib_runtime_t`

| 字段 | 含义 |
|------|------|
| `initialized` | 是否已有首样本 |
| `sample_count` | 累计样本数 |
| `sum_*`, `sum_xx`… | 一阶二阶矩 |
| `min_*`, `max_*` | AABB |
| `angle_bins` | 36 扇区命中位图 |

---

## 14. 编译期宏一览

### `compass_calib.c`（可 `#define` 在包含前或编译选项）

| 宏 | 默认 | 含义 |
|----|------|------|
| `COMPASS_CALIB_MIN_SPAN_UT` | 12 | finalize 要求的最小 XY 跨度 (uT) |
| `COMPASS_CALIB_MIN_SAMPLES` | 120 | finalize 最少样本数；与 `params_valid` 一致 |

### `compass.c`

| 宏 | 默认 | 含义 |
|----|------|------|
| `TICK_MS` | 50 | LVGL 定时器周期 |
| `COMPASS_ANGLE_SMOOTH` | 0.25 | 航向低通 |
| `COMPASS_AUTO_CAL_MIN_COVERAGE` | 0.70 | 自动结束校准 |
| `COMPASS_AUTO_CAL_MIN_SAMPLES` | 220 | 自动结束校准 |
| `COMPASS_MIN_VEC_NORM_UT` | 2.0 | 弱场阈值 |
| `COMPASS_HEADING_OFFSET_DEG` | 0 | 产品零点 |
| `COMPASS_MAG_CHIP_MOUNT_ROTATE_DEG` | 180 | 芯片相对表壳安装角 |
| `COMPASS_NEEDLE_IMG_OFFSET_DEG` | 0 | 指针图修正 |
| `COMPASS_LABEL_MIRROR_EW` | 1 | 标签东西镜像 |
| `COMPASS_DBG` | 0 | 磁力调试日志 |
| `COMPASS_CAL_LOG_EVERY` | 0 | 校准过程周期打印 |
| `COMPASS_DIAG_LOG_EVERY` | 0 | 运行周期诊断 |

### `compass_calib_store.h`

| 宏 | 默认 | 含义 |
|----|------|------|
| `COMPASS_CAL_STORE_LOG` | 1 | load/save 串口日志 |

---

## 15. 调试与常见工程问题

1. **`share_prefs_open failed`**：检查 FAL **`prefdb`**、FlashDB 初始化、`CONFIG_BSP_SHARE_PREFS`。  
2. **校准一直达不到自动结束**：自动门槛是 **220 点 + 70% 覆盖率**；仅 `finalize` 最低 **120 点** — 手动点结束可能仍因 quality/跨度失败。  
3. **coverage 低但明明在转**：硬铁极大时观察 **去中心** 是否仍改善；若环境非均匀场，椭圆模型失效。  
4. **特征值过小**：协方差接近奇异，点云近似直线或静止 — 拒绝 finalize。  
5. **航向整体偏常数**：调 `COMPASS_MAG_CHIP_MOUNT_ROTATE_DEG` / `COMPASS_HEADING_OFFSET_DEG` / 指针 `COMPASS_NEEDLE_IMG_OFFSET_DEG`。  

---

## 附录：与完整 3D 椭球校准的关系

工业界常用 **椭球拟合**（如代数距离最小二乘）同时估计 \(\mathbf{b}\) 与 \(A\)。本实现是其在 **固定先做倾斜补偿 + 只用 xy** 下的 **极简实时版**：计算量小、内存固定，适合 MCU；精度受 2D 投影与线性模型限制。若需更高精度，可在同工程上扩展为三轴采集与 3×3 估计，并重新定义 `compass_calib_params_t` 与 version。

---

*文档随源码维护；算法细节以 `compass.c`、`compass_calib.c` 为准。*
