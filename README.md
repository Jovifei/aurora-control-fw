# Aurora Control Firmware

> 当前开发候选：**v0.8.0 — Protection & Charge Behavior Parity**。
> 功率输出人工总门仍保持关闭，Host/CI通过不代表Keil AC6与300W台架已经完成。

单路异步 Boost 光伏充电控制器固件。v0.8.0以120W V2.7成熟产品行为为基线，适配300W新硬件：默认高功率BOM，可编译切换低功率BOM，支持48/60/72V与铅酸、三元锂、磷酸铁锂、钠离子四类电池。

## 目录

```text
app/
├─ inc/      应用层.h、类型和集中参数
└─ src/      measurement / charger / protection / mppt / power_stage 等业务实现
service/     ISR事件、回调与APP↔Driver唯一桥接；最终PWM/继电器安全复核
driver/
├─ inc/      芯片驱动统一接口
└─ src/      目标MCU驱动实现
board/       PinMap、ADC标定、Flash地址和人工功率门禁
vendor/      CMSIS / Device / DDL
project/     Keil ARM Compiler 6工程
docs/        工程、120W基线、迁移矩阵、300W说明和台架清单
tests/       Host回归与故障注入
tools/       架构、格式、GCC/Clang/Sanitizer/目标语法门禁
```

## v0.8.0安全顺序

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

## v0.8.0阅读入口

- [文档索引](docs/00-文档索引.md)
- [120W V2.7行为与参数基线](docs/22-REF-120W-V2.7行为与参数基线.md)
- [120W→300W行为迁移矩阵](docs/23-120W到300W行为迁移矩阵.md)
- [300W重构工程介绍](docs/24-300W重构工程介绍.md)
- [300W重构录音工程摘要](docs/25-REF-300W重构录音工程摘要.md)
- [v0.8.0参数待确认与台架清单](docs/26-v0.8.0参数待确认与台架清单.md)
- [v0.8.0实现与验证报告](docs/27-v0.8.0实现与验证报告.md)
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

真正解锁前必须完成 `docs/11-Keil编译与台架验收.md` 和 `docs/26-v0.8.0参数待确认与台架清单.md` 的全部P0项目。
