# Aurora Control Firmware

> 当前开发候选：**v0.8.1 — MCU Supply Qualification / Weak-Light Boot Robustness**。
> v0.8.1建立在v0.8.0 Protection & Charge Behavior Parity之上；功率输出人工总门仍保持关闭，Host/CI通过不代表Keil AC6与300W台架已经完成。

单路异步 Boost 光伏充电控制器固件。v0.8.0以120W V2.7成熟产品行为为基线适配300W新硬件；v0.8.1进一步解决弱光时MCU 3.3V辅助供电缓慢上升/抖动导致过早完整初始化的问题。

## 目录

```text
app/
├─ inc/      应用层.h、类型和集中参数
└─ src/      measurement / charger / protection / mppt / power_stage 等业务实现
service/     ISR事件、回调与APP↔Driver唯一桥接；最终PWM/继电器安全复核
driver/
├─ inc/      芯片驱动统一接口
└─ src/      目标MCU驱动实现；v0.8.1 PVD供电资格也在此层
board/       PinMap、ADC标定、PVD启动参数、Flash地址和人工功率门禁
vendor/      CMSIS / Device / DDL
project/     Keil ARM Compiler 6工程
docs/        工程、120W基线、迁移矩阵、300W说明、PVD设计依据和台架清单
tests/       Host回归与故障注入
tools/       架构、格式、GCC/Clang/Sanitizer/目标语法门禁
```

## v0.8.1上电顺序

```text
Reset / POR-PDR释放
→ SystemCoreClock + 1ms SysTick
→ 最小GPIO安全态：GLC/GHC LOW、Relay OFF、Link OFF
→ 配置G32F031 PVD（默认候选2.8V、50us滤波）
→ 等待PVDRDY
→ VDD高于VPVD且连续稳定100ms；中途跌落则重新计时
→ 关闭PVD
→ 才进入Service完整初始化
→ IWDT / PWM / COMP / ADC / UART / APP / Flash
→ 原v0.8.0 PV启动、预充、充电、保护和MPPT状态机
```

弱光下`PVDSTS=LOW`只表示继续等待：**不是Fault，不启动IWDT，不启用PVD中断，也不允许PVD系统复位**。太阳最终增强且VDD连续稳定后，无需人工重新上电即可继续初始化。

## v0.8.0继电器硬安全顺序仍保持不变

```text
Relay OFF
→ 受限Boost先给BST_U充电
→ |BST_U-BAT_U| <= 1.5V连续1s
→ Duty归0，Service物理关PWM
→ Service再次用最新BST_U/BAT_U和故障状态复核
→ 才允许Relay ON
→ 100ms机械稳定并复核压差<=2.5V
→ PWM继续OFF，观察BAT_U完整10s，max-min<=2V
→ 才进入RUN/MPPT
```

任何“先吸合继电器，再慢慢升BST_U”的实现都属于安全回归。

## 阅读入口

- [文档索引](docs/00-文档索引.md)
- [工程接手指南](docs/GUIDE.md)
- [120W V2.7行为与参数基线](docs/22-REF-120W-V2.7行为与参数基线.md)
- [120W→300W行为迁移矩阵](docs/23-120W到300W行为迁移矩阵.md)
- [300W重构工程介绍](docs/24-300W重构工程介绍.md)
- [v0.8.0参数待确认与台架清单](docs/26-v0.8.0参数待确认与台架清单.md)
- [v0.8.0实现与验证报告](docs/27-v0.8.0实现与验证报告.md)
- [G32F031 MCU供电稳定启动与PVD设计依据](docs/28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md)
- [v0.8.1 MCU供电资格修改与验证报告](docs/29-v0.8.1-MCU供电资格修改与验证报告.md)
- [原始/派生表格说明](docs/reference/README.md)

## 软件验证

```bash
python tools/run_checks.py
```

会执行架构/代码规范、Python契约测试、GCC/Clang CTest、ASan/UBSan与Cortex-M0+目标语法检查。

## 当前硬件门禁

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```

真正解锁前必须完成 `docs/11-Keil编译与台架验收.md`、`docs/26-v0.8.0参数待确认与台架清单.md`，以及 `docs/28`/`docs/29` 中新增的VDD/PVD弱光启动台架验证。
