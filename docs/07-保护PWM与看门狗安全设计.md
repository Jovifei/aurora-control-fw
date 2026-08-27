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

快速故障ISR先关PWM，再增加epoch。被中断前取得的旧控制上下文因此失效；低优先级代码恢复执行时会在提交前后再次失败并保持关波。

## 4. 看门狗健康监督

只有`service_watchdog()`可以喂IWDT：

- 任何状态都要求主循环和1 ms控制任务取得进展；
- PRECHARGE、RELAY_SETTLE和RUN还要求ADC完整块被主循环成功消费；
- 启动有500 ms宽限，之后按100 ms健康窗口判断；
- 主循环仍在空转但SysTick/控制停止时，不喂狗；
- ADC在功率状态中停止或DMA覆盖时，不喂狗并锁存故障。

## 5. Flash与故障恢复

功率运行或继电器吸合时禁止Flash擦写。故障恢复后必须重新从WAIT_BATTERY和预充开始，不能直接回到RUN。
