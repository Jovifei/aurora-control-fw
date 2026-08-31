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

## 3. 防止故障ISR后旧代码重开PWM

每次arm或更新Duty均检查：

```text
pending fault == 0
active/latched fault == 0
COMP源无故障
Break未锁存
safety epoch未变化
板级功率门已打开
```

快速故障ISR（例如 COMP 中断）第一动作永远是物理关PWM。如果触发时 PWM 确实在输出，则增加 epoch 导致低优先级代码恢复执行时校验失败并保持关波；如果此时 PWM 未在输出，则仅计入启动期诊断而不锁存，防止上电瞬态扰动引发误保护。

## 4. 软件多级保护网络

| 保护类别 | 保护项 | 触发阈值与延时 | 恢复条件与延时 | 锁存性质 |
|---|---|---|---|---|
| **PV 输入** | 欠压保护 (UV) | < 8V / 1s | > 9V / 1s | 自动恢复 |
| | 过压保护 (OV) | > 55V / 1s | < 54V / 1s | 自动恢复 |
| | 一级软过流 | 1.2× 限流 (14.4A) / 10s | $\le$ 基础限流 / 30s | 硬锁存 (latch) |
| | 二级软过流 | 1.35× 限流 (16.2A) / 1s | $\le$ 基础限流 / 30s | 硬锁存 (latch) |
| | 三级软过流 | 1.5× 限流 (18.0A) / 100ms | $\le$ 基础限流 / 30s | 硬锁存 (latch) |
| | 持续过功率 | 1.2× 额定 (360W) / 5s | 恢复正常 / 30s | 硬锁存 (latch) |
| | 电流合理性 | 运行期 $\le -1\text{A}$ / 10ms 或 停机期 $\ge 3\text{A}$ / 1.5s | 停机下 $\le 0.5\text{A}$ / 30s | 硬锁存 (latch) |
| **电池端** | 欠压保护 (UV) | 按当前档案 $V_{uv}$ / 1s | 按当前档案 $V_{uv\_rec}$ / 1s | 自动恢复 |
| | 一级过压 | 档案 $V_{ov\_slow}$ / 5s | $< V_{cv\_max}$ / 2.5s | 自动恢复 |
| | 二级过压 | 档案 $V_{ov\_slow} + 0.7\text{V}$ / 1s | $< V_{cv\_max}$ / 2.5s | 自动恢复 |
| | 快速过压 (Fast) | 61.8V / 76.4V / 91.0V / **3ms** | $< V_{cv\_max}$ / 2.5s | 自动恢复 |
| | 绝对过压 (Abs) | 统一 93.0V / 1s | $< V_{cv\_max}$ / 2.5s | 自动恢复 |
| **温度与传感器** | MOS 过温 | 105°C / 1s (95~104°C 线性降额) | 95°C / 1s | 自动恢复 |
| | 环境高温 | 55°C / 1s | 50°C / 1s | 自动恢复 |
| | 环境低温 | 铅酸/钠 -20°C / 1s；锂电 0°C / 1s | 铅酸/钠 -15°C；锂电 +5°C / 1s | 自动恢复 |
| | NTC 开路 | ADC 码 $\ge 4093$ / 1s | 码正常 / 1s | 自动恢复 |
| | NTC 短路 | ADC 码 $\le 64$ / 1s | 码正常 / 1s | 自动恢复 |
| **母线与硬件** | 母线 ADC 饱和 | ADC 码 $\ge 4080$ | 码正常 / 1s | 自动恢复 |
| | 硬件 COMP0/2 | 硬件瞬时触发 | 硬件源消失 30s + 清锁存 | 硬锁存 (latch) |

## 5. 看门狗健康监督

只有 `main.c` 中的 `runtime_watchdog()` 可以喂 IWDT：

- **票据管理**：核对 `RUNTIME_WDG_TICKET_MAIN`（主循环轮询）与 `RUNTIME_WDG_TICKET_CONTROL`（1ms 控制调度）；
- **功率状态增强票据**：`PRECHARGE`、`RELAY_SETTLE`、`BAT_STABILITY`、`RUN` 以及 Demo 各阶段还必须核对 `RUNTIME_WDG_TICKET_ADC`（DMA 块处理完整性）；
- **时间窗口**：启动提供 500ms 宽限期，运行期每 100ms 窗口核验一次全套票据，齐全才调用 `drv_watchdog_feed()`；
- **故障阻断**：任何任务卡死、DMA overrun 或未处理异常均导致票据缺失，触发 1000ms 硬件复位。

## 6. Flash与故障恢复

功率运行或继电器吸合时禁止 Flash 擦写。故障恢复后必须重新从 `WAIT_BATTERY` 和预充开始，不能直接回到 RUN。
