# Aurora Control Firmware

> 当前开发候选：**v0.8.3 — Two-Layer Product Architecture**  
> 功率输出人工总门继续保持关闭；Host/CI通过不等于Keil AC6链接和300W实板验收通过。

单路异步Boost光伏充电控制器固件。v0.8.3不改变v0.8.0/v0.8.1已经建立的充电、保护、弱光PVD启动和继电器安全行为，只把首方生产代码收敛为更直接的两层产品架构。

## 目录

```text
app/
├─ inc/
│  ├─ main.h          应用组合根 + 原Service运行接口
│  ├─ app_types.h
│  ├─ app_config.h
│  ├─ debug.h
│  ├─ interrupts.h
│  └─ charger/measurement/mppt/power_stage/protection/protocol/storage/ui.h
└─ src/
   ├─ main.c          原app.c + service.c + target main.c
   ├─ interrupts.c    目标中断桥接，只调用Driver
   ├─ debug.c
   └─ charger/measurement/mppt/power_stage/protection/protocol/storage/ui.c

driver/
├─ inc/
│  ├─ driver.h
│  ├─ board_config.h
│  ├─ drv_board.h
│  └─ drv_adc/drv_comp/drv_flash/drv_io/drv_pwm/drv_system/drv_uart/drv_watchdog.h
└─ src/
   ├─ drv_board.c
   └─ 各目标MCU驱动实现.c

project/
├─ AuroraControl.uvprojx
├─ AuroraControl.sct
└─ README.md

vendor/  docs/  tests/  tools/
```

根目录不再存在 `service/`、`board/` 或 `project/keil/`。

## 两层调用方向

```text
app/src/main.c
├─ 调用APP业务模块
├─ 调用driver/inc/*.h
└─ 不直接访问寄存器/DDL/CMSIS设备对象

app/src/interrupts.c
├─ 只调用Driver ISR接口
├─ 只做搬运、快速关波、事件投递
└─ 不运行MPPT/充电/Flash等重业务

                 ↓

driver/src/*.c
├─ 实现GPIO/ADC/PWM/COMP/Flash/UART/WDT/PVD等硬件契约
├─ 调用vendor CMSIS / Device / DDL
└─ 禁止包含或调用APP代码
```

## 不因架构重构而改变的安全链

### MCU弱光供电资格

```text
Reset
→ 最小GPIO安全态
→ PVD确认VDD高于2.8V候选门限
→ 连续稳定100ms
→ 才初始化IWDT/PWM/COMP/ADC/UART/APP
```

PVD不作为运行期弱光故障，不启用PVD IRQ或PVD系统复位。

### 继电器

```text
Relay OFF
→ 受限Boost先充BST_U
→ |BST_U-BAT_U| <= 1.5V连续1s
→ Duty归0且PWM物理关闭
→ app/main.c用最新电压、故障和PWM状态再次复核
→ Relay ON
→ 100ms后压差复核 <= 2.5V
→ PWM继续OFF，BAT_U观察10s且max-min<=2V
→ RUN/MPPT
```

任何“先吸合继电器，再给BST_U充电”的实现都属于安全回归。

## 关键产品事实

- 默认高功率BOM为300W，保留120W编译Profile；
- MOS从95°C开始降额，105°C/1s停机、95°C/1s恢复；
- 无BAT_I硬件，所有电池电流只能标识为ESTIMATED；
- 单路异步Boost，仅PA15/GLC PWM，不允许重新引入互补PWM/同步MOS；
- PWM运行期只写CCR preload，自然UEV生效，禁止软件UG；
- Flash擦写要求PWM关闭且继电器断开；
- 只有应用运行健康监督允许喂IWDT。

## 文档入口

- [文档索引](docs/00-文档索引.md)
- [工程接手指南](docs/GUIDE.md)
- [v0.8.3两层产品架构重构说明](docs/30-v0.8.3-两层产品架构重构说明.md)
- [v0.8.3迁移与验证报告](docs/31-v0.8.3-迁移与验证报告.md)
- [v0.8.1 MCU供电资格设计依据](docs/28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md)
- [v0.8.0参数待确认与台架清单](docs/26-v0.8.0参数待确认与台架清单.md)

## 软件验证

```bash
python tools/run_checks.py
```

会执行两层架构门禁、代码规范、Python契约测试、GCC/Clang CTest、ASan/UBSan和Cortex-M0+目标语法检查。

## 当前硬件门禁

```c
BOARD_POWER_OUTPUT_ALLOWED == 0
```

真正解锁前仍必须完成Keil AC6真实链接/MAP、PVD弱光波形、ADC/OPA标定、COMP/Break强制触发、继电器预充波形以及低压到额定功率台架。
