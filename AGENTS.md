# Codex / Agent 工作约束

## 1. 当前架构：v0.8.3 两层产品架构

生产代码只允许：

```text
APP → Driver → Vendor
```

根目录禁止重新出现：

```text
service/
board/
project/keil/
legacy_*
tasks/
.bootstrap/
```

APP与Driver都必须采用 `inc/` + `src/`。

## 2. APP规则

- `app/src/main.c`是产品运行根：合并旧`app.c`、`service.c`和target `main.c`职责；允许调用APP业务模块和`driver/inc/*.h`。
- `app/src/interrupts.c`是中断桥接：只允许调用Driver ISR接口、快速关波和事件投递；禁止执行MPPT、充电、存储等重业务。
- `charger.c`、`measurement.c`、`mppt.c`、`power_stage.c`、`protection.c`、`protocol.c`、`storage.c`、`ui.c`保持纯业务，不得直接调用Driver。
- APP可以包含 `driver.h` / `drv_*.h`，但不得包含 `board_config.h`、`g32*`、DDL、CMSIS设备寄存器头。
- APP不得出现GPIOA/ATMR/COMP/DMA寄存器、`DDL_*`或直接寄存器访问。

## 3. Driver规则

- `driver/inc/driver.h`是统一驱动入口，各`drv_*.h`按外设拆分。
- `driver/inc/board_config.h`只放PinMap、ADC/PWM/Flash/PVD/WDT等硬件参数和人工门禁。
- 原`board/board.c/.h`已经合并成 `drv_board.c/.h`。
- `driver/src/*.c`可以调用`vendor/`，禁止包含或调用任何APP代码。
- 更换MCU时优先替换Driver/Vendor，业务模块不应因此修改。

## 4. 代码规范

所有函数定义使用：

```c
/*---------------------------------------------------------------------------*
 * Name        : ...
 * Input       : ...
 * Output      : ...
 * Description : ...
 *---------------------------------------------------------------------------*/
```

- 4空格缩进，禁止Tab和行尾空白；
- 所有文件级`static`变量、常量、声明和函数集中在首个公开函数之前；
- 宏先定义再使用，并写明用途/单位；
- 复杂判断说明原因、安全动作和状态变化；
- 不动态分配内存；不在函数体内新增魔法控制数。

## 5. 永久安全红线

### PWM

- 正常发波最终只能由 `app/src/main.c` 的运行层通过Driver契约执行；业务模块不得直接发波。
- 故障ISR第一动作必须能立即关PWM。
- CCR启用preload；运行期只等待自然UEV，禁止软件UPDATE/UG。
- 首次PWM必须先装载0 Duty，再通过epoch、软件故障、实时Break源、Break锁存和板级总门复核。
- Automatic Output必须关闭；故障恢复不得自动重新MOE。

### Relay

必须严格：

```text
Relay OFF
→ Boost先充BST_U
→ |BST_U-BAT_U|<=1.5V连续1s
→ PWM OFF
→ 运行层实时二次复核
→ Relay ON
→ 100ms复核压差<=2.5V
→ PWM继续OFF
→ BAT_U 10s稳定(max-min<=2V)
→ RUN
```

任何“先Relay ON，再慢慢升BST_U”的修改都禁止进入评审。

### MCU弱光启动

- v0.8.1 PVD供电资格必须保留；默认候选2.8V、连续稳定100ms。
- PVD只用于开机资格，不是运行期弱光Fault。
- 禁止PVD IRQ和PVD系统复位；弱光下只等待。
- IWDT/PWM/COMP/ADC/UART只能在供电资格通过后初始化。

### 其他

- 无BAT_I硬件，电池电流只能标识为ESTIMATED。
- MOS从95°C开始降额；105°C/1s保护、95°C/1s恢复。
- 功率运行或继电器闭合时禁止Flash擦写。
- 只有运行健康监督允许喂IWDT。
- 始终保持 `BOARD_POWER_OUTPUT_ALLOWED=0`，除非Keil、模拟标定、COMP/Break、继电器预充和低压功率台架证据全部人工审核。

## 6. 参数与资料

修改充电/保护参数先读：

- `docs/22-REF-120W-V2.7行为与参数基线.md`
- `docs/23-120W到300W行为迁移矩阵.md`
- `docs/26-v0.8.0参数待确认与台架清单.md`

修改PVD/弱光启动先读：

- `docs/28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md`
- `docs/29-v0.8.1-MCU供电资格修改与验证报告.md`

修改目录/依赖先读：

- `docs/30-v0.8.3-两层产品架构重构说明.md`
- `docs/31-v0.8.3-迁移与验证报告.md`

## 7. 验证

每次修改执行：

```bash
python tools/run_checks.py
```

不得通过删除测试、降低警告或放宽安全门禁让CI变绿。Host/Clang通过不能宣称Keil和实板已经通过。
