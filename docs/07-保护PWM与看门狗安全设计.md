# 07 · 保护、PWM与看门狗安全设计

## 1. COMP0、门极驱动EN和Break

最新版原理图把PB10/AF7的`COMP0_OUT`连接到门极驱动器U6 EN，并有外部上拉。当前软件按以下安全语义实现：

```text
无故障：COMP0_O高，U6允许响应GLC
MOS过流：COMP0_O低，U6 EN立即禁止
                 └→ ATMR低有效Break同时清MOE
```

PB10配置为开漏复用，COMP0输出反相、下降沿中断，ATMR Break配置低有效。这个极性关系仍必须通过拉动比较器输入并同时测量`COMP0_O、GLC、Vgs`确认；验证前`BOARD_GATE_COMP_ROUTE_VALIDATED`保持0。

COMP2用于PV快速过流，走最高优先级比较器中断；ISR第一动作仍是关MOE和通道。

## 2. PWM周期边界

- ARR和CCR均启用preload；
- 运行期只写shadow，禁止软件UG；
- 新Duty在下一个自然UEV才生效；
- 首次发波先写0，等待自然UEV确认，再arm；
- AOE关闭，Break解除后不会自动恢复；
- Break锁存清除和重新arm是两个独立操作。

## 3. 运行期Break必须依据故障前的软件授权分类

硬件Break可能先清MOE，CPU随后才进入COMP/Break ISR。因此，故障后的：

```c
drv_pwm_output_active()
```

不能单独证明故障发生前是否已经发波。v0.10.3把软件授权分成：

```text
ARM_OFF       未申请发波
ARM_WAIT_ZERO 已提交0 CCR、等待自然UEV；仍属于启动瞬态窗口
ARM_ACTIVE    已进入MOE申请窗口或已确认输出；任何Break都按运行故障锁存
```

运行层在调用`drv_pwm_arm()`之前先把状态发布为`ARM_ACTIVE`。这样即使硬件Break立即清MOE，ISR仍能依据故障前的软件授权完成：

```text
物理关PWM
→ safety_epoch递增
→ pending快速故障置位
→ 主循环active/latched锁存
→ 禁止低优先级旧授权重新发波
```

`ARM_WAIT_ZERO`期间没有申请MOE，比较器瞬态只记录启动诊断，不自动提升为运行OCP。若硬件仍报告PWM有效，即使软件状态异常，也按运行故障锁存，作为失配兜底。

启动遗留Break只允许在PWM未获授权、实时故障源消失、没有pending/Protection快速故障时显式清除。一旦进入过`ARM_ACTIVE`，恢复必须等待硬件源连续消失30s并重新走完整准入。

## 4. 软件多级保护网络

| 保护类别 | 保护项 | 触发阈值与延时 | 恢复条件与延时 | 锁存性质 |
|---|---|---|---|---|
| **PV 输入** | 欠压保护 (UV) | < 8V / 1s | > 9V / 1s | 自动恢复 |
| | 过压保护 (OV) | > 55V / 1s | < 54V / 1s | 自动恢复 |
| | 一级软过流 | 1.2× 限流 (14.4A) / 10s | ≤基础限流 / 30s | 硬锁存 |
| | 二级软过流 | 1.35× 限流 (16.2A) / 1s | ≤基础限流 / 30s | 硬锁存 |
| | 三级软过流 | 1.5× 限流 (18.0A) / 100ms | ≤基础限流 / 30s | 硬锁存 |
| | 持续过功率 | 1.2×额定 (360W) / 5s | 恢复正常 / 30s | 硬锁存 |
| | 电流合理性 | 运行期≤-1A / 10ms，或停机期绝对值≥3A / 1.5s | 停机下绝对值≤0.5A / 30s | 硬锁存 |
| **电池端** | 欠压保护 | 按当前档案Vuv / 1s | 按档案Vuv_rec / 1s | 自动恢复 |
| | 一级过压 | 档案Vov_slow / 5s | <Vcv_max / 2.5s | 自动恢复 |
| | 二级过压 | 档案Vov_slow+0.7V / 1s | <Vcv_max / 2.5s | 自动恢复 |
| | 快速过压 | 61.8V / 76.4V / 91.0V / 3ms | <Vcv_max / 2.5s | 自动恢复 |
| | 绝对过压 | 统一93.0V / 1s | <Vcv_max / 2.5s | 自动恢复 |
| **温度与传感器** | MOS过温 | 105°C / 1s；95~104°C线性降额 | 95°C / 1s | 自动恢复 |
| | 环境高温 | 55°C / 1s | 50°C / 1s | 自动恢复 |
| | 环境低温 | 铅酸/钠 -20°C；三元/LFP 0°C / 1s | 铅酸/钠 -15°C；三元/LFP +5°C / 1s | 自动恢复 |
| | NTC开路 | ADC码≥4093 / 1s | 码正常 / 1s | 自动恢复 |
| | NTC短路 | ADC码≤64 / 1s | 码正常 / 1s | 自动恢复 |
| **母线与硬件** | 母线ADC饱和 | ADC码≥4080 | 码正常 / 1s | 独立故障路径 |
| | 硬件COMP0/2 | 硬件瞬时触发 | 源消失30s + 显式清锁存 | 硬锁存 |

## 5. 看门狗健康监督

只有`main.c`中的`runtime_watchdog()`可以喂IWDT：

- 核对`RUNTIME_WDG_TICKET_MAIN`与`RUNTIME_WDG_TICKET_CONTROL`；
- `ZERO_CAL`、`PRECHARGE`、`RELAY_HOLD_OFF`、`RELAY_SETTLE`、`BAT_STABILITY`、`RUN`及Demo各阶段还必须核对ADC票据；
- 启动提供500ms宽限期，运行期每100ms核验一次，齐全才调用`drv_watchdog_feed()`；
- 任务卡死、DMA overrun或ADC长期未处理会导致票据缺失，最终由1000ms硬件看门狗复位。

## 6. Flash与故障恢复

功率运行或继电器吸合时禁止Flash擦写。故障恢复后必须重新从`WAIT_PV`、PV_I零点校准、Battery预充或Demo输出检查开始，不能直接恢复旧Duty或回到RUN。

Host故障注入能够验证软件状态与锁存行为，但不能证明COMP→Break→U6 EN→实际Vgs的板级延迟和极性；该门禁必须由示波器强制触发验收。
