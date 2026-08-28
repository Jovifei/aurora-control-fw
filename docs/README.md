# Aurora Control Firmware 文档入口

当前开发候选：**v0.8.3 — Two-Layer Product Architecture**。

必须区分：源码事实、Host/CI验证、Keil AC6目标编译、板级台架。软件测试通过不能替代功率板验收。

## 推荐阅读顺序

1. [00-文档索引](00-文档索引.md)
2. [GUIDE](GUIDE.md)
3. [30-v0.8.3-两层产品架构重构说明](30-v0.8.3-两层产品架构重构说明.md)
4. [31-v0.8.3-迁移与验证报告](31-v0.8.3-迁移与验证报告.md)
5. [22-REF-120W-V2.7行为与参数基线](22-REF-120W-V2.7行为与参数基线.md)
6. [23-120W到300W行为迁移矩阵](23-120W到300W行为迁移矩阵.md)
7. [24-300W重构工程介绍](24-300W重构工程介绍.md)
8. [26-v0.8.0参数待确认与台架清单](26-v0.8.0参数待确认与台架清单.md)
9. [28-REF-G32F031-MCU供电稳定启动与PVD设计依据](28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md)
10. [29-v0.8.1-MCU供电资格修改与验证报告](29-v0.8.1-MCU供电资格修改与验证报告.md)
11. [07-保护PWM与看门狗安全设计](07-保护PWM与看门狗安全设计.md)
12. [11-Keil编译与台架验收](11-Keil编译与台架验收.md)

## 当前生产架构

```text
APP → Driver → Vendor
```

根目录不再存在Service和Board层。`app/src/main.c`负责应用运行编排，`app/src/interrupts.c`负责轻量中断桥接；所有芯片寄存器和DDL操作都在Driver/Vendor侧。

## 两条不能破坏的启动链

MCU：

```text
GPIO安全态 → PVD确认VDD连续稳定 → 才IWDT/PWM/COMP/ADC/UART/APP
```

Relay：

```text
Relay OFF → Boost先充BST_U → 压差稳定 → PWM OFF → 实时复核 → Relay ON → BAT稳定 → RUN
```

当前人工总门：

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```
