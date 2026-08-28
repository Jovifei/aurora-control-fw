# Aurora Control Firmware 文档入口

必须区分四类证据：源码事实、Host/CI验证、Keil AC6目标编译、板级台架。软件测试通过不能替代功率板验收。

## v0.8.1推荐阅读顺序

1. [00-文档索引](00-文档索引.md)
2. [GUIDE](GUIDE.md)
3. [22-REF-120W-V2.7行为与参数基线](22-REF-120W-V2.7行为与参数基线.md)
4. [23-120W到300W行为迁移矩阵](23-120W到300W行为迁移矩阵.md)
5. [24-300W重构工程介绍](24-300W重构工程介绍.md)
6. [26-v0.8.0参数待确认与台架清单](26-v0.8.0参数待确认与台架清单.md)
7. [27-v0.8.0实现与验证报告](27-v0.8.0实现与验证报告.md)
8. [28-REF-G32F031-MCU供电稳定启动与PVD设计依据](28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md)
9. [29-v0.8.1-MCU供电资格修改与验证报告](29-v0.8.1-MCU供电资格修改与验证报告.md)
10. [07-保护PWM与看门狗安全设计](07-保护PWM与看门狗安全设计.md)
11. [11-Keil编译与台架验收](11-Keil编译与台架验收.md)
12. [17-参数标定与Codex交接清单](17-参数标定与Codex交接清单.md)

## v0.8.1弱光上电资格

完整外设初始化前先执行：

```text
System/SysTick
→ 最小GPIO安全态
→ G32F031 PVD Ready
→ VDD连续高于配置门限并稳定达到宏配置时间
→ 关闭PVD
→ IWDT / PWM / COMP / ADC / UART / APP / Flash
```

PVD只作为启动资格门，不是运行期“MCU弱光保护”。`PVDSTS=LOW`时继续等待并重新累计稳定时间，不产生APP Fault、不启动IWDT、不使用PVD IRQ或PVD系统复位。默认候选参数全部集中在 `board/board_config.h`，最终仍需VDD/NRST/GLC/Relay示波器台架冻结。

## 不可破坏的继电器顺序

`PRECHARGE`期间Relay必须OFF，先受限Boost把BST_U充到接近BAT_U；压差连续稳定后先停PWM，Service在真正写Relay GPIO前再次检查最新压差和故障，之后才能吸合Relay。Relay后还要保持PWM OFF完成10s电池稳定性验证。

当前人工总门：

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```
