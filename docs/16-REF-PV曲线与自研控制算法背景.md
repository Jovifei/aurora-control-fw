# 16 · REF · PV曲线与自研控制算法背景

> 来源说明：以下正文整理自项目早期的学习文档，原文中的旧功率档、同步整流器件、旧MCU和闭源算法只作为历史背景。当前产品代码以 `05-自研MPPT算法与控制链.md`、最终原理图和当前源码为准：单路异步Boost、无BAT_I、源码可见的P-V斜率搜索、PV参考电压PI和功率包络。
> 
> 保留本文的目的，是记录PV曲线、Duty作用机理和算法选择依据；不得从本文的历史器件名称或历史数值反推当前板级配置。

# PV曲线、Duty作用机理与适合本项目的MPPT控制算法


> **当前更新基线：FW 2.0.0 / 2026-08-21**    
> - 用户已知产品条件：光伏板只确认额定功率约 **120 W**，没有 `Voc/Vmp/Isc/Imp`、温度系数和串并联规格；设备目前能够运行。后端面向电动车 **48/60/72 V** 电池平台，支持铅酸、三元锂、磷酸铁锂和钠离子档案。  
> - 源码直接配置：系统时钟 60 MHz、MPPT/PWM 目标 50 kHz、`Mppt_Duty_Sum=1200`、`Mppt_L=77 µH`、`Mppt_Dead_Time=20`（源码注释约 333.3 ns）、PV额定接入窗口 13–55 V、PV限流 7 A、PV软件过流阈值 8 A、输入功率上限 120 W。  
> - 证据标签：`[SRC]` 当前源码直接事实；`[SCH]` 原理图直接事实；`[PHY]` 功率电子/电化学通用原理；`[INF]` 工程推导；`[TEST]` 必须由BOM、手册、编译产物或波形确认。

> 当前源码直接证明：`Fun_MPPT_FUNC()`来自闭源库，但集成参数明确命名 `MPPT_PNO_Time`，`main.c`注释其为 **Perturb and Observe（P&O，扰动观察法）** 的最大功率点保持时间；库还导出 `MPPT_Recalibrate`、`MPPT_Return_Boost_Mode`、IIR滤波等符号。因此可以把现有算法归入P&O家族，但仍不能从二进制接口精确还原每一个扰动、回扫、限流和重启公式。  
> 本文将“现有算法事实”“建议保留的生产基线”“可选的增量电导升级方案”分开书写。  
> **本节新增参考源：** 用户上传的微型逆变器 `Mppt.c/.h`、`flyback.c/.h`、`fullbridge.c/.h`、`main.c/.h`、`Customer.h`。从“适合本项目的推荐控制架构”开始，明确区分微逆源码事实、120W充电器当前事实和跨平台推荐方案。

---

## 1. 光伏板不是理想电压源

理想电压源可在电流变化时保持电压不变；PV板的电压、电流由以下因素共同决定：

- 光照强度 `G`；
- 电池片温度 `T`；
- 外部负载；
- 串并联结构；
- 局部阴影；
- 旁路二极管；
- 老化和失配。

PV工作点必须落在其I-V曲线上。

---

## 2. 光伏单二极管模型

常见近似：

$$
I=I_{ph}-I_0\left[\exp\left(\frac{V+IR_s}{nV_T}\right)-1\right]-\frac{V+IR_s}{R_{sh}}
$$

其中：

- `Iph`：光生电流，主要随光照增加；
- `I0`：二极管反向饱和电流；
- `Rs`：串联电阻；
- `Rsh`：并联漏电阻；
- `n`：理想因子；
- `VT`：热电压。

不需要在MCU里实时解这个方程，但它解释了为什么PV既不是恒压源，也不是恒流源。

---

## 3. I-V与P-V曲线

### 3.1 I-V曲线

```text
I
│ Isc ───────────────┐
│                    │
│                    └──────\
│                            \
│                             \ 0
└─────────────────────────────── V
0              Vmp            Voc
```

### 3.2 P-V曲线

$$
P=VI
$$

```text
P
│                  /\
│                 /  \
│                /MPP \
│_______________/______\________ V
0             Vmp      Voc
```

### 3.3 三个关键点

| 点 | 电压 | 电流 | 功率 |
|---|---:|---:|---:|
| 短路点 | 0 | `Isc` | 0 |
| 最大功率点 | `Vmp` | `Imp` | 最大 |
| 开路点 | `Voc` | 0 | 0 |

因此“拉取电流越大越好”是错误的。Duty太大把PV拉到短路侧时，电流接近Isc，但电压大幅下降，功率反而降低。

---

![PV的IV与PV曲线](assets/figures/fig08-PV的IV与PV曲线.png)

## 4. 光照、温度与阴影怎样移动曲线

### 4.1 光照增强

通常：

- `Isc/Imp`显著增加；
- `Voc/Vmp`仅缓慢增加；
- 最大功率明显增加。

### 4.2 温度升高

通常：

- `Voc/Vmp`下降；
- 电流略升；
- 最大功率下降。

所以固定一个永不变化的 `Vmp_ref` 不够，需要MPPT跟踪。

### 4.3 局部阴影

旁路二极管可能使P-V曲线出现多个局部峰值：

```text
P
│      /\        /\
│     /  \______/  \
│____/______________\____ V
     局部峰       全局峰
```

普通P&O或增量电导可能停在局部峰。单块小功率组件局部阴影风险相对可控，但若产品允许多串/复杂阴影，应增加周期性全局扫描。

---

## 4A. 本项目没有光伏铭牌参数，MPPT文档应怎样写

当前只知道“120 W且能运行”，未知 `Voc/Vmp/Isc/Imp`。所以：

- 不能把17 V写成实际 `Vmp`；
- 不能用固定 `0.75Voc` 之类比例作为生产目标；
- 不能判断55 V软件上限对最低温 `Voc` 是否有足够裕量；
- 不能判断7 A限流与面板 `Isc/Imp` 的关系；
- 算法评价必须依赖实际采样曲线或PV模拟器，而不是假设铭牌。

代码本身提供了一个不依赖铭牌的工作包络：13–55 V允许进入额定窗口，15 V作为慢启动分界，12–17 V限功率由50 W线性抬到120 W，17 V以上最多120 W；PV电流上限7 A。因此，现有P&O仍可在未知面板参数下通过实时 `V/I/P`寻找工作点。

## 5. Q6物理占空比为什么会改变PV工作点

![Duty与PV工作点移动](assets/figures/fig09-Duty与PV工作点移动.png)

## 5.1 电感平均方程

CCM理想平均模型：

$$
L\frac{dI_L}{dt}=V_{PV}-(1-D_{Q6})V_{BAT}
$$

短时间内把 `Vpv`、`Vbat`看作不变：

```text
D_Q6增加
→ (1-D)Vbat减小
→ 电感平均电压增加
→ IL上升
→ Boost拉取的PV电流增加
```

## 5.2 输入电容节点方程

$$
C_{in}\frac{dV_{PV}}{dt}=I_{panel}(V_{PV},G,T)-I_L
$$

当Boost突然拉得更重：

$$
I_L>I_{panel}
$$

C14/C16放电，PV电压下降。

当Boost拉得更轻：

$$
I_L<I_{panel}
$$

PV给C14/C16充电，PV电压上升。

## 5.3 完整因果链

```text
D_Q6↑
→ L3每拍正伏秒↑
→ IL平均值↑
→ 输入电流↑
→ C14/C16放电
→ Vpv↓
→ 工作点向短路侧移动
```

```text
D_Q6↓
→ IL平均值↓
→ 输入电流↓
→ C14/C16被PV充电
→ Vpv↑
→ 工作点向开路侧移动
```

## 5.4 当前软件CCR方向

当前 `ADC.c`使用 `(Mppt_Duty_Sum-Duty)`参与低侧电感边界计算，历史资料也把Q6视为互补输出。因此工程控制中很可能：

```text
要增加D_Q6 → 减小CCR/Duty
要减小D_Q6 → 增大CCR/Duty
```

但当前资料存在CH2/CH2N标签冲突，最终必须通过PA14/Q6实际Vgs确认。

---

## 6. MPPT真正要控制的不是“输出电压”

因为输出接电池：

- 电池把输出母线钳在自身端电压附近；
- Duty变化主要改变输入电流和传输功率；
- 电池SOC、温度和化学体系决定允许的输出电压/电流；
- MPPT不能为了追求PV最大功率而突破电池安全边界。

因此正确权限关系：

$$
P_{cmd}=\min(P_{MPP},P_{battery},P_{hardware},P_{thermal})
$$

优先级：

```text
硬件保护
> 电池/BMS限制
> 功率级电流温度限制
> 充电阶段TC/CC/CV/FC
> MPPT最大功率
```

---

# 第一类算法：扰动观察法 P&O

## 7. 基本思想

采样：

$$
P_k=V_kI_k
$$

计算：

$$
\Delta P=P_k-P_{k-1}
$$

$$
\Delta V=V_k-V_{k-1}
$$

判断：

| `ΔP` | `ΔV` | 结论 |
|---:|---:|---|
| >0 | >0 | 提高V使功率增加，继续提高V |
| >0 | <0 | 降低V使功率增加，继续降低V |
| <0 | >0 | 提高V使功率下降，反向降低V |
| <0 | <0 | 降低V使功率下降，反向提高V |

## 8. P&O优点

- 实现简单；
- 计算量低；
- 不依赖PV参数；
- 适合小MCU；
- 易于调试。

## 9. P&O缺点

- 在MPP附近持续摆动；
- 步长大：响应快、稳态损失大；
- 步长小：稳态好、跟踪慢；
- 光照突然变化时，算法可能把环境变化误认为自身扰动效果；
- 局部阴影时可能停在局部峰。

## 10. 为什么不建议直接扰动本工程软件CCR

本工程存在：

- CCR与Q6物理Duty可能反向；
- 电池电压动态变化；
- DCM/CCM切换；
- Q5同步接入；
- TC/CC/CV约束；
- Duty限幅和shadow/UEV；

直接“CCR加一，看功率”容易把拓扑极性、限流和状态切换混在一起。更推荐MPPT输出 `Vpv_ref`，再由独立内环调Duty。

---

# 第二类算法：增量电导 Incremental Conductance

## 11. 数学原理

$$
P=VI
$$

$$
\frac{dP}{dV}=I+V\frac{dI}{dV}
$$

MPP处：

$$
\frac{dP}{dV}=0
$$

所以：

$$
\frac{dI}{dV}=-\frac{I}{V}
$$

离散化：

$$
\frac{\Delta I}{\Delta V}\approx-\frac{I}{V}
$$

### 11.1 MPP左侧：PV电压偏低

$$
\frac{dP}{dV}>0
$$

需要提高PV电压，即减轻负载、减小Q6物理Duty。

### 11.2 MPP右侧：PV电压偏高

$$
\frac{dP}{dV}<0
$$

需要降低PV电压，即加重负载、增大Q6物理Duty。

### 11.3 MPP附近

$$
\frac{dP}{dV}\approx0
$$

保持目标电压。

## 12. 增量电导优点

- 理论上可识别到达MPP而停止大幅扰动；
- 对快速光照变化的判断优于基础P&O；
- 很适合输出PV电压参考值；
- 可配合自适应步长。

## 13. 增量电导缺点

- 对电压、电流差分噪声敏感；
- `ΔV`很小时有除法问题；
- 需要死区和滤波；
- 固定点实现需注意溢出和精度；
- 局部阴影仍可能停在局部峰。

## 14. 无除法形式

避免直接计算 `ΔI/ΔV`，可比较：

$$
S=I\Delta V+V\Delta I
$$

因为：

$$
\Delta P\approx I\Delta V+V\Delta I
$$

- `S>ε`：MPP左侧，应提高Vpv_ref；
- `S<-ε`：MPP右侧，应降低Vpv_ref；
- `|S|≤ε`：MPP附近。

这种形式更适合定点MCU。

---

# 第三类：其他方法及是否适合

## 15. 恒电压/分数开路电压

$$
V_{MPP}\approx kV_{OC}
$$

优点简单，缺点需周期断开测Voc、精度受组件和温度影响。可作为启动初值，不建议作为主算法。

## 16. 分数短路电流

$$
I_{MPP}\approx kI_{SC}
$$

需要短路或额外传感，工程侵入性高，不适合本板主算法。

## 17. 扫描法

周期扫过一定Vpv范围，记录全局最大功率。适合局部阴影全局MPPT，但会暂时离开MPP并扰动充电。

## 18. 模型/智能算法

模糊控制、神经网络、粒子群等可以处理复杂曲线，但对120 W嵌入式充电器通常增加验证难度。安全产品优先可解释、可边界证明的算法。

---

![P&O与增量电导对比](assets/figures/fig11-P与增量电导算法对比.png)

# 当前源码实际采用的MPPT接口与P&O证据

## 18A. 直接源码证据

`Type_MPPT`接口直接包含：

- 当前/上次 `pv_V、pv_I、pv_P`；
- `Duty、Duty_Prev、DutySize`；
- `pv_P_max_Duty、pv_P_max_Power、MPPT_MaxPP_flag`；
- `MPPT_Disturbance_Pmin/Pmax`；
- `MPPT_PNO_Time`；
- 开路偏移、功率变化阈值、重算和重启标志；
- 电池电压/电流、PV电流/功率限制。

`main.c`配置：

| 参数 | 当前值 | 工程单位/含义 |
|---|---:|---|
| `DutySize_Min/Max` | 1 / 1 | 每次比较值步长固定1计数 |
| `DutySize_Rate` | 0.1 | 库内部步长率参数 |
| `MPPT_Disturbance_Pmin/Pmax` | 10 / 30 | 按0.01 W推测为0.10/0.30 W扰动阈值 |
| `MPPT_CHG_Deltapv_I` | 15 | 0.15 A |
| `MPPT_CHG_Deltapv_V` | 5 | 0.05 V |
| `MPPT_PNO_Time` | 5 | Mode0每10 ms调用时约50 ms保持/观察窗口 |
| `MPPT_PV_V_Ratio` | 75 | 库内开路/参考比例参数，具体用法闭源 |

因此现有实现不是“完全未知算法”，而是**闭源P&O家族实现**；未知的是内部状态机、扰动方向、快速辐照判断和恢复细节。

## 18B. 现有P&O外围限制

每次调用库之前，应用层先更新：

- PV功率上限：电压降额与MOS温度降额取更小者；
- PV电流上限：7.0/7.2 A迟滞；
- 电池电流上限：TC约1 A、CC约3 A、CV由电压环和终止电流约0.3 A限制；
- 电池电压上限：按化学体系和48/60/72 V档案；
- 浮充标志：仅铅酸进入FC。

所以库输出的Duty是“MPPT与充电约束共同作用”的结果，不能把任何一次非MPP工作都判成算法失效。

# 适合本项目的推荐控制架构：参考微型逆变器方案的可移植重构版

> **目标不是照搬反激微逆代码，而是尽可能复用其成熟的控制思想：**“慢速MPPT搜索只调整PV参考电压 `mpptVRef`；中速参考电压PI把电压误差转换为功率请求；快速拓扑执行器再把功率请求转换成开关周期、导通时间或占空比；所有命令必须服从功率、电池、温度和硬件保护包络。”  
> 本节同时区分：`[SRC][微逆]` 上传的微型逆变器源码事实；`[SRC][充电器]` 当前120W Boost充电器源码事实；`[REC]` 面向本项目重新开发与跨平台移植的推荐方案；`[TEST]` 必须通过PV模拟器、示波器或实板测试确定的参数。

---

## 19. 先给最终建议

当前120W充电器最值得采用的，不是“单独把P&O换成增量电导”，而是把控制链重构为下列**多时间尺度、参考电压型MPPT架构**：

```text
PV快速采样与安全保护
        ↓
固定窗口平均 Vpv / Ipv / Ppv
        ↓
外层MPPT搜索：根据 ΔP/ΔV 调整 Vpv_ref
        ↓
中层PV参考电压PI：根据 Vpv - Vpv_ref 计算理论功率请求 P_mppt_theory
        ↓
功率包络：电池TC/CC/CV/FC、120W、7A、温度、BMS、硬件能力共同限幅
        ↓
最终功率命令 P_cmd
        ↓
Boost执行器：P_cmd → Ipv_ref → Q6物理Duty → T=20µs / Ton
        ↓
PWM shadow → UEV提交 → Q6异步启动 → 满足条件后允许Q5同步整流
```

这比“MPPT算法直接改CCR”多了两个关键隔离层：

1. **MPPT只决定PV应该工作在哪个电压附近；**
2. **拓扑执行器才决定Q6应该导通多久。**

这样做有四个直接收益：

- MPPT搜索算法与HT32的CH2/CH2N、CCR方向、死区编码解耦；
- 电池CC/CV限制可以在功率请求层统一否决MPPT；
- 同一套MPPT核心可移植到STM32、GD32、NXP、Infineon或Host仿真；
- 未来即使把固定50kHz Boost换成电流模式、变频或其他拓扑，外层 `mpptVRef` 搜索仍可复用。

![微逆MPPT链路映射到Boost充电器](assets/figures/fig20-微逆MPPT链路映射到Boost充电器.png)

---

## 20. 微型逆变器方案

### 20.1 MPPT外层：搜索的是 `mpptVRef`，不是直接搜索PWM

`[SRC][微逆]` `MPPTRoutine()`的主要数据链是：

```text
inputVoltageAverage + inputCurrentAverage
→ inputPowerAverage
→ deltaV / deltaP
→ MppSlope ≈ ΔP / ΔV
→ 自适应step
→ mpptVRef += step
```

源码直接计算：

$$
P_k=V_{pv,k}I_{pv,k}
$$

$$
\Delta V=V_{pv,k}-V_{pv,k-1}
$$

$$
\Delta P=P_k-P_{k-1}
$$

$$
Slope_{PV}\approx\frac{\Delta P}{\Delta V}
$$

并使用定点放大：

```c
MppSlope = (deltaP << 8) / deltaV;
```

其方向判断为：

- `MppSlope > 0`：位于P-V曲线峰值左侧，应该提高PV参考电压；
- `MppSlope < 0`：位于峰值右侧，应该降低PV参考电压；
- `MppSlope ≈ 0`：已在MPP附近，保持或只做极小扰动。

这是一种**直接P-V斜率法**。它与经典增量电导法的关系是：

$$
\frac{dP}{dV}=I+V\frac{dI}{dV}
$$

经典增量电导比较的是：

$$
\frac{\Delta I}{\Delta V}\quad\text{与}\quad-\frac{I}{V}
$$

微逆方案则直接用：

$$
\frac{\Delta P}{\Delta V}
$$

两者在理想测量条件下使用的是同一个峰值判据：

```text
斜率 > 0：MPP左侧
斜率 = 0：MPP
斜率 < 0：MPP右侧
```

所以本项目没有必要为了“算法名称”强行改成教科书形式；优先复用微逆已经验证过的**直接P-V斜率 + 参考电压搜索**更合理。

### 20.2 微逆的实际MPPT更新周期约为80ms

`[SRC][微逆]` 源码里虽然 `NPeriodsCalculateMPPTVref()`由100µs状态机频繁调用，`MPPTRoutine()`注释也写过“400µs调用”，但真正执行受数据就绪标志控制：

1. 正向过零时，每个完整电网周期约20ms计算一次平均数据；
2. 第2个周期记录 `inputPowerMiddleAverage`；
3. 第4个周期才调用一次 `MPPTRoutine()`；
4. 因而50Hz下外层参考电压更新约为：

$$
T_{mppt}\approx4\times20ms=80ms
$$

这类“函数被高频调用，但算法只在数据窗口完整时真正更新”的写法，应在跨平台版本中改成显式的 `data_ready` 和 `dt_us`，避免读代码时误把100µs当成MPPT带宽。

### 20.3 中层：PV参考电压PI计算“功率型因子”

`[SRC][微逆]` `VRefController()`计算：

$$
e_v=V_{pv,avg,1period}-V_{pv,ref}
$$

然后：

$$
P_{theory}=K_pe_v+\int K_ie_vdt
$$

代码把这个输出保存为 `mpptFactorcalculate`。它不是MOS占空比，而是一个**功率型控制量**，后续作为 `poRaw` 进入反激拓扑计算。

这个符号方向很重要：

- `Vpv > Vref`：说明负载拉得太轻，误差为正，PI提高功率请求；
- 功率请求增大：反激从PV拉更多电流；
- PV电压下降，回到 `Vref`；
- `Vpv < Vref` 时过程反向。

这与本Boost项目的物理关系完全兼容。

### 20.4 微逆源码中的PI并非简单固定“0.7/0.1与0.1/0.01”两套

微逆注释描述了正常模式和突发模式两组PI参数，但当前上传实现实际采用：

```text
基础 Kp = 0.1
基础 Ki = 0.01
动态倍率 k = 1～7
```

于是实际有效范围约为：

$$
K_p\approx0.1\sim0.7
$$

$$
K_i\approx0.01\sim0.07
$$

倍率随 `mpptFactorcalculate`变化：低功率时增益更大，高功率时增益更小。这个“增益调度”思想值得复用，但**数值不能直接移植到120W Boost**，原因包括：

- 电压、电流和功率定点单位不同；
- 反激与Boost小信号模型不同；
- Boost存在右半平面零点；
- 本项目输出由电池钳位，微逆输出由电网和全桥约束；
- 采样滤波、执行周期、内环带宽不同。

### 20.5 多重功率包络

微逆的 `VRefController()`在PI之后依次叠加：

- PV高电压功率表；
- 温度降额；
- 输入电压相关电流限制；
- 用户功率限制；
- 电网频率功率限制；
- 防逆流功率限制；
- 积分限幅；
- 最小/最大功率限制。

最值得复用的不是某一张具体表，而是：

> **PI先计算“理论上希望多少功率”，然后所有外部约束只做上限裁决，最终得到可执行功率。**

### 20.6 `mpptFactor → poRaw → T/Ton → PWM`

`[SRC][微逆]` 反激控制链为：

```text
mpptFactor
→ poRaw
→ 固定频率或变频公式
→ T、TOn、isBoth
→ PWM shadow/update
→ 下一个安全更新点生效
```

`FlybackControl()`每16µs运行一次，但通过6次分频约每96µs更新一次周期与占空比。更新函数在故障存在时直接返回，避免“故障已经关断、普通更新链又把PWM改回来”。

这种**算法输出与PWM提交分离、故障门控位于提交入口**的设计非常值得本充电器复用。

### 20.7 当前微逆源码的Burst设计意图与实际状态

微逆设计意图是：

```text
理论功率低于40W
→ 计算N个电网周期
→ 只在其中1个周期用较高功率工作
→ 其余N-1个周期停开关
→ 平均功率仍等于低功率目标

理论功率高于48W
→ 退出Burst
→ 恢复每周期连续控制
```

但 `[SRC][微逆]` 当前上传的 `AdjustMpptFactorWithBurstMode()`中，进入/退出Burst的主体代码被注释；实际执行的是：

```text
triggeredControlPeriod = 1
mpptFactor = powerFactor
burstModeActiveFlag不被该函数置1
```

因此：

> 40W/48W Burst是**保留的设计方案**，不是当前上传版本已经启用的运行事实。

这一区分必须保留，否则会把注释中的架构意图误写成当前行为。

---

## 21. 微逆方案中哪些内容可以直接复用，哪些必须改造

| 微逆设计 | 本120W Boost是否采用 | 处理方式 |
|---|---|---|
| MPPT只更新 `mpptVRef` | **采用** | 直接作为推荐外层接口 |
| `ΔP/ΔV`判断峰值左右 | **采用** | 作为首选可移植算法；经典IncCond可做影子对照 |
| 20ms平均、约80ms更新参考电压 | **采用思想** | 无电网过零，改成固定时间窗口20/40/80ms |
| 启动时从Voc附近快速向下搜索 | **采用** | 但步长改为相对Voc或mV，不照搬定点值 `-300` |
| 斜率大则步长大，峰值附近步长小 | **采用** | 加明确噪声死区、限幅与防覆盖逻辑 |
| 参考电压PI输出功率因子 | **采用** | 改成物理量 `p_mppt_theory_mW` |
| 电压、温度、电流、用户限制统一限幅 | **采用并扩展** | 加入电池TC/CC/CV/FC、BMS、120W和7A约束 |
| 积分限幅与低功率重初始化 | **采用** | 增加跟踪/限幅模式的无扰切换 |
| 功率变化速率限制 | **采用** | 复用 `step>>3`思想，改成显式mW/s、mV/s |
| `mpptFactor → poRaw → T/Ton` | **采用分层思想** | 改成 `Pcmd → Ipv_ref → D_Q6 → T/Ton` |
| 反激固定/变频切换 | **不直接采用** | 当前Boost保持50kHz，除非重新设计磁性器件与EMI |
| 40W/48W按电网周期Burst | **不直接照搬** | 改成可选的DC脉冲包模式，阈值需台架标定 |
| 电网过零、全桥、PLL、频率功率调节 | **不采用** | 属于并网后端，不属于电池充电器 |
| 防逆流总电网功率PI | **不直接采用** | 替换为电池/BMS允许功率和反向电池电流限制 |
| 45～60V微逆高压功率表 | **不照搬数值** | 使用本项目13～55V窗口及现有PV/温度降额表 |

---

## 22. 推荐的七层控制结构

![推荐MPPT多层控制架构](assets/figures/fig21-推荐MPPT多层控制架构.png)

```text
第0层：硬件保护与不可绕过的安全监督
第1层：采样、校准和多时间窗口统计
第2层：电池/温度/功率级允许包络
第3层：MPPT参考电压搜索（约80ms）
第4层：Vref PI → 理论功率请求（约10ms）
第5层：功率到Boost命令的快速执行器（约100µs～1ms）
第6层：PWM shadow/UEV提交与Q5同步整流监督（20µs/1ms）
```

### 22.1 第0层：安全监督器

此层具有最高权限，MPPT没有资格覆盖：

- CMP/BK0/Break；
- 电池过压；
- PV或电池过流；
- L3峰值电流；
- MOS温度；
- ADC超时或采样无效；
- LVD；
- BMS禁止充电；
- 反向大电流；
- 故障锁存后的禁止重发波。

输出只有两种：

```text
ALLOW_CONTROL
FORCE_SAFE_OFF
```

### 22.2 第1层：采样与窗口统计

输入包括：

```text
Vpv_fast / Ipv_fast
Vpv_slow / Ipv_slow
Vbat / Ibat
MOS温度 / 环境温度
保护输入与BMS状态
```

输出包括：

```text
Vpv_1ms、Ipv_1ms
Vpv_avg_20ms、Ipv_avg_20ms、Ppv_avg_20ms
Ppv_mid_40ms
Vpv_avg_80ms、Ipv_avg_80ms、Ppv_avg_80ms
measurement_valid
```

### 22.3 第2层：允许功率包络

统一计算：

$$
P_{allow}=\min
\left(
P_{rated},
P_{pv\_current},
P_{pv\_voltage},
P_{battery\_stage},
P_{thermal},
P_{BMS},
P_{hardware}
\right)
$$

对当前项目可写成：

$$
P_{rated}=120W
$$

$$
P_{pv\_current}\le V_{pv}\times7A
$$

$$
P_{battery\_stage}\le V_{bat}\times I_{TC/CC/CV/FC,allow}
$$

电池阶段优先级高于MPP：

- TC：只允许小电流；
- CC：不超过充电电流上限；
- CV：保持电压，电流自然下降；
- FC：仅铅酸浮充；
- Complete：停止或等待复充。

### 22.4 第3层：MPPT参考电压搜索

输入：20～80ms窗口后的V/I/P。  
输出：`v_pv_ref_mV`。  
它不认识HT32寄存器，也不直接产生Duty。

### 22.5 第4层：PV参考电压PI

输入：

```text
Vpv_avg_10/20ms
Vpv_ref
P_allow
工作模式
```

输出：

```text
P_mppt_theory
```

### 22.6 第5层：Boost执行器

输入：最终功率命令 `P_cmd`。  
输出：物理Q6占空比、周期和导通时间：

```text
T_cmd
Ton_q6_cmd
D_q6_cmd
```

### 22.7 第6层：PWM提交和同步整流

- 把物理Duty映射为平台CCR；
- 写入shadow；
- 在UEV提交；
- 故障时拒绝提交；
- 首次启动只允许Q6异步；
- Q5同步接入由独立电流判据决定。

---

## 23. 推荐多时间尺度调度

微逆使用“16µs快速控制、10ms电压PI、20ms平均、80ms MPPT参考更新”。对本50kHz Boost，可映射为：

![推荐多速率控制时序](assets/figures/fig22-推荐多速率控制时序.png)

| 时间尺度 | 本项目推荐任务 | 当前源码可复用基础 |
|---:|---|---|
| 20µs / 每个PWM周期 | UEV、快速电流采样、ADC超时、反向电流、故障关断、shadow提交 | `MCTM0_UP_IRQHandler()` / `fun_MCTM_Code()` |
| 100µs（每5个周期） | 可选平均电流内环、Duty前馈修正、斜率与输出限幅 | 由UEV分频实现，跨平台时显式任务化 |
| 1ms | 慢速V/I融合、Q5同步进退、快速功率限制 | 当前 `Fun_Get_ADC_Value()` |
| 10ms | 电池状态机、Vref PI、功率包络、功率斜率限制 | 当前Mode0状态机节拍 |
| 20ms | 形成一个PV平均窗口 | 用20次1ms样本，不再依赖电网过零 |
| 40ms | 保存中间功率 `P_middle` | 复用微逆中间时刻思想 |
| 80ms | 更新一次 `mpptVRef` | 复用微逆4个20ms周期结构 |
| 100ms～1s | 温度、故障恢复、Voc重估条件、日志 | 当前已有100ms/1s级任务 |

### 23.1 为什么不在20µs ISR里算MPPT

MPPT依赖平均功率趋势，不需要50kHz更新。把除法、斜率、状态机和日志塞进逐周期ISR会：

- 增加最大中断延迟；
- 干扰ADC采样相位；
- 增大故障关断抖动；
- 让跨平台WCET难验证；
- 对MPPT收益几乎没有帮助。

20µs ISR只做**有界、可证明执行时间**的动作。

---

## 24. 建议的数据模型与物理单位

当前微逆大量使用Q格式，当前充电器大量使用0.01V、0.01A、0.01W。跨平台重写时，控制核心建议统一使用显式单位：

```c
typedef struct {
    int32_t v_pv_mV;
    int32_t i_pv_mA;
    int32_t p_pv_mW;
    int32_t v_bat_mV;
    int32_t i_bat_mA;
    int32_t p_bat_mW;
    int32_t mos_temp_cdeg;
    uint32_t timestamp_us;
    bool valid;
} power_sample_t;

typedef struct {
    int32_t v_avg_mV;
    int32_t i_avg_mA;
    int32_t p_avg_mW;
    int32_t p_middle_mW;
    uint32_t duration_us;
    bool ready;
} pv_window_t;
```

功率计算必须先扩位：

$$
P_{mW}=\frac{V_{mV}I_{mA}}{1000}
$$

代码应使用64位中间量：

```c
int32_t p_mW = (int32_t)(((int64_t)v_mV * i_mA) / 1000);
```

不要在算法核心中暴露Q15/Q17；平台或DSP优化层可以在证明等价后替换实现。

---

## 25. 开路电压 `Voc` 的获取与初始化

微逆把 `mpptVRef`初始化为 `openCircuitVoltage`，再用较大的负步长快速向MPP方向移动。这个思想可以复用，但本项目没有光伏铭牌，必须把 `Voc`当作**运行时测量值**。

### 25.1 推荐Voc测量条件

```text
Q5关闭
Q6关闭
Boost PWM保持安全态
输入电容C14/C16充到PV开路电压
等待电压变化率进入稳定区
连续采样并取中值/平均
```

有效条件建议包括：

- PV电压在13～55V硬件允许窗口内；
- `|dV/dt|`低于阈值；
- PV输入电流接近零；
- 没有电池/BMS/温度故障；
- 避免在云影快速变化时把瞬时值当作长期Voc。

### 25.2 不应直接照搬“Voc与Vmp固定差5～6V”

微逆对应的组件约60V开路、48V附近MPP，5～6V是其具体硬件经验。对可能只有17V量级工作点的120W板，固定减5～6V会产生过大比例偏差。

推荐初始化：

$$
V_{ref,init}=clamp(k_{voc}V_{oc},V_{ref,min},V_{ref,max})
$$

其中 `k_voc`可先取经验范围，再由实测收敛；最终不应把它当成产品永久常数。

另一种更贴近微逆的方式是：

```text
Vref = Voc
→ 每80ms向下走一个启动步长
→ 一旦P明显增加且V已离开开路区
→ 转入斜率跟踪
```

---

## 26. 外层MPPT：直接P-V斜率搜索

### 26.1 基本计算

每次80ms更新：

$$
P_k=V_kI_k
$$

$$
\Delta V=V_k-V_{k-1}
$$

$$
\Delta P=P_k-P_{k-1}
$$

当 `|ΔV|`足够大时：

$$
S_k=\frac{\Delta P}{\Delta V}
$$

也可避免除法，直接使用符号等价量：

$$
S'_{k}=\Delta P\cdot sign(\Delta V)
$$

但若要让步长与斜率幅值成比例，仍需归一化或定点除法。

### 26.2 噪声死区

若：

$$
|\Delta V|<V_{noise}
$$

或：

$$
|\Delta P|<P_{noise}
$$

则保持：

```text
Slope = 0
step = 0
```

死区应由实际ADC日志统计得到，而不是照搬微逆的原始整数 `20`、`4`。

### 26.3 方向

```text
Slope > +deadband
→ P随V增加而增加
→ 当前在MPP左侧
→ 增大Vpv_ref
→ 减轻Boost负载

Slope < -deadband
→ P随V增加而下降
→ 当前在MPP右侧
→ 减小Vpv_ref
→ 加重Boost负载
```

### 26.4 自适应步长

推荐：

$$
step_{raw}=K_sS_k
$$

$$
step=clamp(step_{raw},-step_{max},step_{max})
$$

并按状态选择 `stepMax`：

| 状态              | 步长策略            |     |     |     |        |
| --------------- | --------------- | --- | --- | --- | ------ |
| `FAST_DESCENT`  | 大负步长，快速离开Voc区   |     |     |     |        |
| `TRACK_FAR`     | 斜率大，大步长         |     |     |     |        |
| `TRACK_NEAR`    | `               | ΔV  | `、` | ΔP  | `小，小步长 |
| `POWER_LIMITED` | 冻结或慢速更新，不追求MPP  |     |     |     |        |
| `BURST`         | 只在完整有效能量窗口后更新   |     |     |     |        |
| `RATE_LIMITED`  | `step`再缩小，例如除以8 |     |     |     |        |

### 26.5 参考电压限幅

必须同时满足：

```text
Vpv_ref > 13V + 安全裕量
Vpv_ref < min(Voc - 裕量, 55V - 硬件裕量)
Vpv_ref不能一次跳过过大的电压范围
```

对未知面板，限幅最好同时包含绝对值和相对Voc：

$$
V_{ref,min}=\max(V_{hw,min}+margin, k_{min}V_{oc})
$$

$$
V_{ref,max}=\min(V_{hw,max}-margin, V_{oc}-V_{oc,margin})
$$

---

## 27. `inputPowerMiddleAverage`如何使用

微逆在第2个20ms窗口保存中间功率：

$$
P_{mid}
$$

并计算：

$$
P_{mapped}=2P_{mid}-P_{end}
$$

这个量可用于估计动态变化过程的对称映射功率，试图减小辐照变化对扰动判断的影响。

但是 `[SRC][微逆]` 当前上传的 `MPPTRoutine()`虽然计算了 `inputPowerNew`，实际 `deltaP`仍使用：

```c
deltaP = inputPowerAverage - preinputPower;
```

而不是：

```c
deltaP = inputPowerNew - preinputPower;
```

因此本项目建议分成两种可测试模式：

### 模式A：源码等价模式

$$
\Delta P=P_{end}-P_{prev}
$$

优点：简单、与微逆当前行为一致。

### 模式B：动态映射实验模式

$$
P_{effective}=2P_{mid}-P_{end}
$$

$$
\Delta P=P_{effective}-P_{prev,effective}
$$

只有在PV模拟器的辐照阶跃测试中证明误判率下降，才能启用模式B。不能因为变量存在就默认它已经有效。

---

## 28. 微逆源码中两个移植时必须修正的细节

### 28.1 `stepMax=50`可能被后续逻辑覆盖

微逆先在 `|deltaV|<20`时设置：

```c
stepMax = 50;
```

但后面 `k`判断中的 `else`会在大多数 `k>=1`情况下再次写：

```c
stepMax = 200;
```

因此“稳定阶段最大步长50”在当前源码中未必真正保持。

跨平台版本应显式写成：

```c
step_max = step_max_by_voltage;
step_max = min(step_max, step_max_by_power);
step_max = min(step_max, step_max_by_mode);
```

避免后写覆盖前写。

### 28.2 注释周期与有效执行周期必须分开

`MPPTRoutine()`注释中的“400µs调用”不能代表有效更新周期。推荐所有算法函数带显式参数：

```c
mppt_vref_update(..., uint32_t dt_us);
```

并只在80ms窗口就绪时调用一次。

---

## 29. 中层Vref PI：把电压误差变成功率请求

![PV参考电压PI到Boost PWM链路](assets/figures/fig23-VrefPI到BoostPWM命令链.png)

### 29.1 误差定义

沿用微逆符号：

$$
e_v=V_{pv}-V_{pv,ref}
$$

物理意义：

- `e_v > 0`：PV电压过高，Boost拉得太轻，需要增加功率；
- `e_v < 0`：PV电压过低，Boost拉得太重，需要降低功率。

### 29.2 PI输出

$$
P_{mppt,theory}=K_pe_v+x_I
$$

$$
x_I(k)=x_I(k-1)+K_ie_vT_s
$$

推荐输出使用mW，而不是无单位 `mpptFactor`：

```c
int32_t p_mppt_theory_mW;
```

### 29.3 增益调度

可以复用微逆“低功率增益大、高功率增益小”的思想：

```text
低功率：输入电容和负载变化慢，可用相对高增益快速拉回Vref
中高功率：电感电流和Boost RHP零点影响更明显，降低外环增益
限功率/CV：冻结或反算积分器，避免积分堆积
```

推荐结构：

```c
gain = gain_schedule(p_cmd, mode);
kp = kp_base * gain;
ki = ki_base * gain;
```

但实际整定必须基于：

- `Cin≈300µF`；
- `L≈70～77µH`；
- 50kHz固定开关；
- 1ms/10ms采样延迟；
- 电池电压范围；
- 线缆与PV源阻抗；
- Boost右半平面零点。

### 29.4 抗积分饱和

最终允许功率：

$$
P_{cmd}=clamp(P_{mppt,theory},P_{min},P_{allow})
$$

当输出被上限钳住时，应采用条件积分或反算：

```c
if (!saturated || drives_out_of_saturation) {
    integrator += ki * error * dt;
}
```

或：

$$
x_I\leftarrow x_I+K_{aw}(P_{cmd}-P_{mppt,theory})
$$

### 29.5 低功率长期无效时重新捕获Voc

可复用微逆的30s思路：

```text
理论功率长期≤0或Vref环无法建立
→ 关闭Boost
→ 等待PV恢复开路
→ 重新测Voc
→ 清PI积分
→ 回到FAST_DESCENT
```

但“30s”是策略参数，应结合产品启动体验和阴影工况验证。

---

## 30. 第2层功率包络：把微逆的多限制机制改造成电池充电约束

### 30.1 最终功率命令

$$
P_{cmd}=\min
\left(
P_{mppt,theory},
P_{rated},
P_{pvI},
P_{battery},
P_{thermal},
P_{BMS},
P_{hardware}
\right)
$$

### 30.2 当前项目的主要限制

```text
额定功率：120W
PV电流：7A正常上限，软件/硬件保护另有更高故障阈值
PV电压：13～55V接入范围
电池阶段：TC / CC / CV / 铅酸FC
电池平台：48 / 60 / 72V，多化学体系
MOS与环境温度：按当前降额表
BMS：若有charge_enable或故障信号，具有否决权
```

### 30.3 电池阶段换成功率上限

TC或CC：

$$
P_{battery}=V_{bat}I_{stage,max}
$$

CV：

```text
电压控制器给出允许充电电流
→ P_battery = Vbat × I_allow
```

铅酸FC：

```text
只维持浮充电压和小电流
→ 多余太阳能必须舍弃
```

### 30.4 充电受限时MPPT如何处理

当：

```text
P_allow < P_mpp_estimate
```

系统进入 `MPPT_LIMITED`：

- 不再强求PV工作在MPP；
- `mpptVRef`冻结或只做慢速漂移；
- PI积分器跟踪实际 `P_cmd`；
- 退出限幅后无扰恢复，不允许突然跳到旧积分值。

这不是MPPT效率低，而是电池安全优先。

---

## 31. 可选的电池功率校正环

微逆还有 `powerController()`，利用输入功率与估算效率修正最终 `powerFactor`。本项目已有 `Vbat`和`Ibat`，可以将这个思想改成一个**很慢的功率校正环**：

$$
P_{bat,meas}=V_{bat}I_{bat}
$$

$$
e_P=P_{cmd}-\frac{P_{bat,meas}}{\eta_{est}}
$$

校正量：

$$
P_{trim}=PI_P(e_P)
$$

最终：

$$
P_{actuator}=P_{cmd}+P_{trim}
$$

注意：

- 它不是首版必需功能；
- 带宽必须显著低于PV电压PI和输入电流环；
- 不能与电池CC/CV控制互相争夺；
- `ηest`可先用分段效率表；
- 若功率测量噪声大，宁可不用，也不要增加低频振荡。

---

## 32. 低功率模式：复用Burst思想，但改成DC脉冲包

![低功率脉冲包模式](assets/figures/fig24-低功率脉冲包模式.png)

### 32.1 为什么不能照搬“40W/48W、按电网周期开关”

微逆的阈值服务于800W级双路反激、20ms电网周期和其轻载效率。本项目只有120W、固定50kHz Boost、输出接电池，所以：

- 40W已经是额定功率的三分之一，不一定属于轻载；
- 本项目没有电网周期可用；
- 脉冲包会直接表现为电池充电电流脉动；
- BMS、母线电容和同步整流策略都可能受影响。

因此阈值必须由效率曲线、最小稳定电流和电池纹波测试确定。

### 32.2 推荐DC脉冲包算法

设最低高效率工作功率为：

$$
P_{packet,on}
$$

当前平均功率请求为：

$$
P_{cmd}<P_{burst,enter}
$$

计算：

$$
N=ceil\left(\frac{P_{packet,on}}{P_{cmd}}\right)
$$

然后：

```text
一个控制帧ON：按P_packet,on运行
N-1个控制帧OFF：Q5/Q6安全关断
平均功率 ≈ P_packet,on / N
```

控制帧可从10ms或20ms起步，不要逐个PWM周期跳开关。

### 32.3 进入与退出迟滞

```text
Pcmd < P_burst_enter 持续T_enter → 进入
Pcmd > P_burst_exit  持续T_exit  → 退出
P_burst_exit > P_burst_enter
```

必须设置：

- 最小ON时间；
- 最小OFF时间；
- 最大N；
- 最小驻留时间；
- 电池电流纹波上限；
- 每次ON先异步启动；
- Q5只有在正向连续电流成立后才同步。

### 32.4 ARS不能跟着Burst频繁动作

Q9/Q11的ARS支路属于电池接入和防反接，不应每10～20ms开关。Burst只控制Q5/Q6主Boost PWM；电池负极主连接保持稳定。

### 32.5 Burst期间如何计算MPPT

不能把OFF帧的零电流直接混入普通斜率：

- 只统计完整ON帧的V/I/P；或
- 按能量对整个N帧窗口平均；
- `ΔP/ΔV`更新必须等待可比的有效窗口；
- Burst刚切换时冻结一次MPPT更新，避免模式变化被误判为光照变化。

### 32.6 本项目默认建议

```text
第一版：保留异步DCM，不启用Burst
第二版：测出5～120W效率曲线和最小稳定电流
第三版：若低功率开关损耗确实显著，再启用脉冲包影子逻辑
第四版：低功率台架验证通过后才实控
```

---

## 33. 从功率命令到Boost的 `T`、`Ton` 和Duty

微逆的拓扑层通过反激公式同时计算周期 `T`和导通时间 `TOn`。本Boost当前设计为固定50kHz，所以推荐：

$$
T_{cmd}=\frac{1}{50kHz}=20\mu s
$$

### 33.1 功率命令转换为输入电流参考

$$
I_{pv,ref}=\frac{P_{actuator}}{\max(V_{pv},V_{safe,min})}
$$

同时限幅：

$$
0\le I_{pv,ref}\le7A
$$

### 33.2 Boost占空比前馈

理想Boost：

$$
D_{ff}=1-\frac{V_{pv}}{V_{bat}}
$$

它只提供工作点估计，不能单独作为闭环命令。

### 33.3 推荐快速平均电流环

误差：

$$
e_i=I_{pv,ref}-I_{pv}
$$

命令：

$$
D_{Q6,cmd}=D_{ff}+K_{pi}e_i+K_{ii}\int e_i dt
$$

然后：

$$
D_{Q6,cmd}=clamp(D_{Q6,cmd},D_{min},D_{max})
$$

导通时间：

$$
T_{on,Q6}=D_{Q6,cmd}T_{cmd}
$$

### 33.4 为什么推荐“Vref PI输出功率 + 电流内环”，而不是Vref PI直接输出Duty

- 功率命令容易与120W、7A、电池CC/CV统一比较；
- 电流环能直接限制L3平均电流；
- Duty前馈处理大范围48～72V电池电压变化；
- 外层MPPT和电池管理不依赖当前MCU的CCR方向；
- 未来换Buck-Boost或多相拓扑时，上层接口不变。

### 33.5 首版最小改造方案

如果当前平台暂时不增加快速电流PI，可先采用：

```text
mpptVRef外层
→ Vref PI输出Pcmd
→ Pcmd转换成Ipv_ref
→ 根据Ipv误差以小步长修正当前Duty
```

但仍要把“功率命令”和“CCR映射”分开，不应让MPPT模块直接写 `HT_MCTM0`。

---

## 34. 物理Duty到当前HT32 CCR的映射

算法层输出：

```c
d_q6_q15
```

平台层负责：

```text
D_Q6
→ Q6真实Ton
→ CH2/CH2N主互补关系
→ CCR
→ deadtime
→ shadow
```

根据当前工程高置信推断：

$$
D_{Q6}\propto Mppt\_Duty\_Sum-CCR
$$

可暂写：

$$
CCR_{cmd}\approx DutySum(1-D_{Q6,cmd})
$$

但最终必须用Q6 `LGW-PGND`实测确认。

当前软件上下限：

```text
CCR 108～1092
```

不能直接解释成物理Duty 9%～91%，还要包含：

- ARR端点语义；
- PWM1主/互补极性；
- 死区；
- 驱动器反相；
- 实际Vgs传播延迟。

---

## 35. PWM shadow、UEV和故障门控

### 35.1 推荐提交链

```text
控制算法生成物理命令
→ 验证范围、变化率和状态
→ 转换为shadow CCR
→ 再次检查fault_latched / Break
→ 在UEV提交
→ 记录active command序号
```

### 35.2 复用微逆“故障期间拒绝更新”的思想

微逆在 `pwm*_update_period_and_duty()`入口先检查故障；存在故障就直接返回，不触发PWM更新。充电器应把同一原则放进唯一HAL入口：

```c
bool hal_pwm_stage_command(const pwm_command_t *cmd)
{
    if (fault_latched || hal_break_is_active()) {
        return false;
    }
    write_shadow(cmd);
    return true;
}
```

### 35.3 当前首次Boost仍保留两个UEV门禁

新算法接入当前板时继续保留：

```text
写安全shadow
→ 等待两个UEV
→ Q5保持GPIO低
→ 只接Q6异步PWM
→ 开CHMOE
→ 再检查保护
```

任何跨平台版本都要实现“等价的安全提交语义”，不能只保证函数名相同。

---

## 36. Q5同步整流必须与MPPT解耦

MPPT只请求PV功率；Q5是否同步由独立监督器决定。

推荐状态：

```text
SYNC_DISABLED
→ ASYNC_RUNNING
→ SYNC_QUALIFY
→ SYNC_ENABLED
→ SYNC_EXIT
```

进入条件：

- Q6异步运行稳定；
- 电感/PV电流明显为正；
- 电流高于边界并持续确认；
- 无Burst OFF；
- 无故障；
- Vgs/驱动电源有效。

退出条件优先：

- PV电流接近0；
- 电池电流过小；
- 出现负电流趋势；
- Burst即将OFF；
- Duty或电压落入不允许同步区域；
- 任意保护。

当前源码“进入同步约200ms确认、退出约1ms或UEV快速退出”的不对称思想可以保留。

---

## 37. 推荐状态机：四套状态互相独立

不要把所有逻辑塞进一个巨大状态变量。

### 37.1 安全状态机

```text
SAFE_OFF
→ PRECHECK
→ ARS_PRECHARGE
→ PWM_ARMED
→ RUN
→ FAULT_LATCHED
→ RECOVERY_WAIT
→ SAFE_OFF
```

### 37.2 电池充电状态机

```text
TC → CC → CV → FC(仅铅酸) → COMPLETE → RECHARGE
```

### 37.3 MPPT状态机

```text
MPPT_DISABLED
→ VOC_CAPTURE
→ FAST_DESCENT
→ TRACK
↔ POWER_LIMITED
↔ BURST_OPTIONAL
→ RESCAN_OPTIONAL
→ MPPT_DISABLED
```

### 37.4 同步整流状态机

```text
ASYNC_ONLY → QUALIFY_SYNC → SYNC_ON → FAST_EXIT → ASYNC_ONLY
```

这样可以保证：

- 电池进入CV不会被误写成MPPT故障；
- Burst退出不会直接操作ARS；
- Q5退出同步不需要重置整个MPPT；
- 故障锁存可以一票否决所有普通状态机。

---

## 38. 推荐核心数据接口

```c
typedef enum {
    MPPT_OFF,
    MPPT_VOC_CAPTURE,
    MPPT_FAST_DESCENT,
    MPPT_TRACK,
    MPPT_LIMITED,
    MPPT_BURST,
    MPPT_RESCAN
} mppt_mode_t;

typedef struct {
    int32_t voc_mV;
    int32_t v_ref_mV;
    int32_t v_prev_mV;
    int32_t p_prev_mW;
    int32_t p_mid_mW;
    int32_t slope_q;
    int32_t step_mV;
    int32_t integrator_mW;
    int32_t p_theory_mW;
    int32_t p_command_mW;
    mppt_mode_t mode;
} mppt_controller_t;

typedef struct {
    int32_t p_rated_max_mW;
    int32_t p_pv_current_max_mW;
    int32_t p_battery_max_mW;
    int32_t p_thermal_max_mW;
    int32_t p_bms_max_mW;
    int32_t p_hw_max_mW;
    bool charge_allowed;
} mppt_limits_t;

typedef struct {
    int32_t p_cmd_mW;
    int32_t i_pv_ref_mA;
    int32_t d_q6_q15;
    uint32_t pwm_period_ns;
    uint32_t q6_ton_ns;
    bool boost_enable;
    bool sync_rect_request;
    bool burst_on_window;
    uint32_t reason_flags;
} boost_control_command_t;
```

---

## 39. 推荐伪代码：完整多速率实现

### 39.1 1ms测量融合

```c
void control_1ms_tick(const raw_adc_t *raw)
{
    power_sample_t s = measurement_convert_and_validate(raw);

    if (!s.valid) {
        safety_force_off(FAULT_ADC_INVALID);
        return;
    }

    pv_window_accumulate_20ms(&window, &s);
    sync_rectifier_update_1ms(&sync_ctrl, &s, safety_status());
    fast_limits_update_1ms(&limits, &s);
}
```

### 39.2 20/40/80ms窗口

```c
void control_20ms_epoch(void)
{
    pv_window_t w = pv_window_close_and_restart(&window);

    mppt_window_count++;

    if (mppt_window_count == 2) {
        mppt.p_mid_mW = w.p_avg_mW;
    }

    if (mppt_window_count >= 4) {
        mppt_window_count = 0;
        mppt_vref_search_80ms(&mppt, &w, &limits);
    }
}
```

### 39.3 80ms参考电压搜索

```c
void mppt_vref_search_80ms(mppt_controller_t *m,
                           const pv_window_t *w,
                           const mppt_limits_t *lim)
{
    if (!w->ready || !lim->charge_allowed) {
        return;
    }

    if (m->mode == MPPT_LIMITED) {
        m->v_prev_mV = w->v_avg_mV;
        m->p_prev_mW = w->p_avg_mW;
        return;
    }

    int32_t dV = w->v_avg_mV - m->v_prev_mV;
    int32_t dP = w->p_avg_mW - m->p_prev_mW;

    if (abs(dV) < DV_NOISE_MV || abs(dP) < DP_NOISE_MW) {
        m->step_mV = 0;
    } else {
        int32_t slope = fixed_div_q(dP, dV, SLOPE_Q);
        int32_t step_max = select_step_max(m->mode, dV, dP, m->p_command_mW);
        m->step_mV = clamp_i32(slope_to_step(slope), -step_max, step_max);
    }

    if (m->mode == MPPT_FAST_DESCENT) {
        m->step_mV = -startup_step_from_voc(m->voc_mV);
    }

    if (power_slew_limit_active()) {
        m->step_mV /= 8;
    }

    m->v_ref_mV = clamp_vref(m->v_ref_mV + m->step_mV, m->voc_mV);
    m->v_prev_mV = w->v_avg_mV;
    m->p_prev_mW = w->p_avg_mW;
}
```

### 39.4 10ms Vref PI和功率包络

```c
void control_10ms_tick(const power_sample_t *s)
{
    mppt_limits_t lim = compute_all_power_limits(s, battery_profile(), bms_status());

    int32_t error_mV = s->v_pv_mV - mppt.v_ref_mV;
    pi_gain_t gain = mppt_gain_schedule(mppt.p_command_mW, mppt.mode);

    int32_t p_unsat_mW = pi_voltage_to_power(&mppt, error_mV, gain, 10000);
    int32_t p_allow_mW = min_all_limits(&lim);

    mppt.p_theory_mW = p_unsat_mW;
    mppt.p_command_mW = clamp_i32(p_unsat_mW, 0, p_allow_mW);

    pi_antiwindup_track(&mppt, mppt.p_command_mW, p_unsat_mW);
    burst_manager_update_10ms(&burst, &mppt, s);
}
```

### 39.5 100µs Boost快速执行器

```c
void boost_control_100us(const power_sample_t *s)
{
    if (!safety_control_allowed() || !burst.on_window) {
        hal_pwm_force_safe_off(REASON_NOT_ALLOWED);
        return;
    }

    int32_t p_cmd = mppt.p_command_mW;
    int32_t i_ref_mA = safe_power_to_current(p_cmd, s->v_pv_mV);

    int32_t d_ff_q15 = boost_duty_feedforward_q15(s->v_pv_mV, s->v_bat_mV);
    int32_t d_pi_q15 = input_current_pi_update_q15(i_ref_mA - s->i_pv_mA, 100);
    int32_t d_q6_q15 = clamp_q15(d_ff_q15 + d_pi_q15,
                                 D_Q6_MIN_Q15,
                                 D_Q6_MAX_Q15);

    boost_control_command_t cmd = {
        .p_cmd_mW = p_cmd,
        .i_pv_ref_mA = i_ref_mA,
        .d_q6_q15 = d_q6_q15,
        .pwm_period_ns = 20000,
        .q6_ton_ns = q15_mul_u32(d_q6_q15, 20000),
        .boost_enable = true,
        .sync_rect_request = sync_ctrl.allow_sync,
        .burst_on_window = true,
    };

    hal_pwm_stage_command(&cmd); // 只写shadow
}
```

### 39.6 20µs UEV提交

```c
void pwm_update_event_isr(void)
{
    fast_measure_and_protect();

    if (fault_latched() || hal_break_is_active()) {
        hal_pwm_force_safe_off(REASON_FAULT);
        return;
    }

    hal_pwm_commit_staged_command();
}
```

---

## 40. 推荐参数表：哪些可以先定，哪些必须测试

| 参数 | 初始建议 | 证据状态 |
|---|---|---|
| PWM周期 | 20µs / 50kHz | `[SRC][充电器]` 当前配置 |
| 快速执行器周期 | 100µs起步 | `[REC]` UEV每5次分频；需WCET与稳定性验证 |
| 慢测量周期 | 1ms | `[SRC][充电器]` 当前基础 |
| Vref PI周期 | 10ms | `[REC]` 复用微逆结构并匹配当前状态机 |
| 平均窗口 | 20ms | `[REC]` 复用微逆一个电网周期窗口 |
| 中间功率 | 40ms | `[REC]` 复用微逆第2窗口 |
| MPPT更新 | 80ms | `[REC]` 复用微逆第4窗口 |
| `Voc`初始化 | 运行时测量 | `[REC][TEST]` 无面板铭牌 |
| 启动快速步长 | 按Voc比例或mV定义 | `[TEST]` 不照搬 `-300` |
| 斜率死区 | 按实际噪声3σ以上 | `[TEST]` |
| Vref PI增益 | 由小到大台架整定 | `[TEST]` 微逆0.1～0.7/0.01～0.07仅作结构参考 |
| Burst进入/退出阈值 | 默认关闭 | `[TEST]` 必须先测效率和电池纹波 |
| Duty上下限 | 先保持当前软件边界 | `[SRC][充电器]`，物理映射需Vgs确认 |
| Q5同步阈值 | 保留当前进入慢、退出快 | `[SRC][充电器]`，再用同步采样优化 |

---

## 41. 为什么本项目首选“微逆同源斜率法”，而不是立即切经典IncCond

### 41.1 直接P-V斜率的优点

- 与上传微逆成熟架构一致；
- 直接使用已经计算的P、V平均值；
- 易于做自适应步长；
- 输出天然是 `Vpv_ref`；
- 跨平台实现简单；
- 便于与现有P&O做影子对比。

### 41.2 经典IncCond的价值

经典IncCond对快速辐照变化理论上更容易区分“环境变化”和“控制扰动”，可作为并行影子算法：

$$
S_{inc}=I\Delta V+V\Delta I
$$

避免除法：

```text
S_inc > 0：MPP左侧
S_inc = 0：MPP
S_inc < 0：MPP右侧
```

### 41.3 推荐策略

```text
主候选：直接ΔP/ΔV参考电压搜索
影子算法：无除法IncCond
同一批Golden Vector离线比较
只有IncCond在快速光照或阴影下稳定显著更优，才考虑切换
```

不是为了追求“算法名更高级”而换算法。

---

## 42. 与当前闭源P&O的迁移路径

### 阶段0：冻结当前基线

记录：

- 当前闭源库Duty；
- Vpv/Ipv/Ppv；
- Vbat/Ibat；
- 电池阶段；
- 温度和所有限幅；
- Q5同步状态；
- 故障与恢复；
- 关键波形。

### 阶段1：只上线测量窗口

不改变控制，只建立20/40/80ms窗口和运行日志。

### 阶段2：影子 `mpptVRef`

新斜率算法只计算：

```text
Voc
Slope
step
Vpv_ref_shadow
```

不控制PWM。

### 阶段3：影子Vref PI

计算：

```text
P_mppt_theory_shadow
P_allow
P_cmd_shadow
Ipv_ref_shadow
D_q6_shadow
```

仍不写CCR。

### 阶段4：低功率异步实控

- 限流电源/PV模拟器；
- Q5强制关闭；
- 只控制Q6；
- 从低Duty、低功率开始；
- 验证Vref环方向、Ton和电流限制。

### 阶段5：全充电状态机

依次验证TC、CC、CV、铅酸FC、完成和复充。

### 阶段6：同步整流

独立启用Q5，验证：

- 实际Vgs死区；
- 零电流退出；
- 反向电流；
- 体二极管反向恢复；
- 故障后无重发波。

### 阶段7：可选Burst与全局扫描

只有在效率曲线和局部阴影需求证明必要时加入。

---

## 43. Golden Vector与Host闭环仿真

### 43.1 Golden Vector字段

```text
timestamp
Vpv / Ipv / Ppv
Vbat / Ibat / Pbat
Voc / Vref / slope / step
P_theory / P_allow / P_cmd
Ipv_ref / Dq6 / CCR / Ton
charge_state / mppt_state / sync_state / burst_state
fault_flags / limit_reason
```

### 43.2 单元测试

- 正斜率、负斜率、零斜率；
- `ΔV=0`；
- 噪声死区；
- 启动快速下降；
- 步长限幅；
- `Vref`上下限；
- PI饱和和反饱和；
- TC/CC/CV限功率；
- 故障时命令归零；
- Burst进入、退出和N周期计算；
- 平台CCR映射方向。

### 43.3 闭环模型

Host模型至少包含：

- PV I-V/P-V模型；
- 输入电容；
- L3平均模型；
- 电池电压源和内阻；
- 采样延迟与噪声；
- Duty限幅；
- 电池阶段限制；
- 温度降额；
- 故障注入。

### 43.4 必须用PV模拟器验证的工况

- 恒定辐照稳态；
- 低→高辐照阶跃；
- 高→低辐照阶跃；
- 温度导致Voc变化；
- 多峰局部阴影曲线；
- 电池从CC进入CV；
- BMS突然降低允许充电功率；
- 低功率DCM；
- 可选Burst；
- 保护动作和恢复。

---

## 44. 推荐验收指标

| 指标 | 定义 |
|---|---|
| 静态MPPT效率 | `实际稳定PV功率 / PV模拟器理论MPP功率` |
| 动态MPPT效率 | 辐照变化全过程采集能量 / 理论可采集能量 |
| 收敛时间 | 新工况后进入MPP功率误差带所需时间 |
| 稳态摆动 | MPP附近功率峰峰值或RMS |
| Vref跟踪误差 | `Vpv - Vref`均值与峰值 |
| 电池CC超调 | `Ibat_peak - I_CC_limit` |
| CV超调 | `Vbat_peak - V_CV_target` |
| Duty变化率 | 单次和单位时间最大 `ΔD/ΔCCR` |
| 电感峰值 | 不超过L3、MOS、比较器和SOA边界 |
| Q5反向电流 | 同步退出前最小电流及持续时间 |
| Burst纹波 | 电池电流峰峰值、母线纹波、BMS响应 |
| 保护延迟 | 故障输入到Q5/Q6实际Vgs关断时间 |
| 重发波 | 故障锁存期间必须为0次 |

---

## 45. 本次推荐架构的核心结论

1. **优先复用微逆的控制分层，而不是照搬反激公式。**
2. MPPT外层采用直接 `ΔP/ΔV`斜率，输出 `Vpv_ref`；经典IncCond作为影子算法比较。
3. 参考电压PI不直接输出CCR，而输出物理功率请求 `P_mppt_theory`。
4. 电池TC/CC/CV/FC、120W、7A、温度、BMS和硬件能力共同形成 `P_allow`，并拥有高于MPPT的权限。
5. Boost拓扑层将 `P_cmd → Ipv_ref → D_Q6 → T=20µs、Ton`，保持50kHz固定频率。
6. 当前微逆的变频反激、全桥、过零、电网频率和40/48W阈值不能直接移植。
7. Burst思想可以保留为可选DC脉冲包，但当前默认关闭，必须先测低功率效率和电池电流纹波。
8. Q5同步整流与MPPT解耦；任何Burst ON窗口也先异步启动。
9. PWM更新必须经过shadow、UEV和故障门控，复用微逆“故障期间拒绝普通更新”的原则。
10. 当前微逆源码中 `inputPowerNew`未用于 `deltaP`、`stepMax`可能被覆盖、Burst主体被注释，这些细节不能原样带入新平台。
11. 跨平台算法核心统一使用mV/mA/mW和显式 `dt_us`，不访问HT32寄存器。
12. 新算法应先影子运行、Golden Vector和PV模拟器验证，再进入低功率异步实控，最后才启用同步整流与额定功率。
