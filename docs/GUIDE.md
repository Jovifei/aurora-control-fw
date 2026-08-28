# 工程接手与修改指南

## 1. 第一次接手

先运行：

```bash
python tools/run_checks.py
```

预期至少看到：

```text
ARCHITECTURE CHECK: PASS
CODE STYLE CHECK: PASS
Python contract tests: PASS
GCC strict build + CTest: PASS
Clang strict build + CTest: PASS
Clang ASan/UBSan: PASS
Cortex-M0+ target syntax: PASS
```

Host通过不等于Keil链接或实板验收通过。

## 2. v0.8.3两层架构

```text
app/
├─ inc/
└─ src/

driver/
├─ inc/
└─ src/

vendor/
project/
docs/
tests/
tools/
```

生产根目录禁止出现 `service/`、`board/`、`project/keil/`。

调用方向只有：

```text
app → driver → vendor
```

### APP

- `app/src/main.c`：系统入口、事件调度、APP业务组合、PWM/Relay命令落实、Flash和WDT健康监督；只能通过Driver接口碰硬件。
- `app/src/interrupts.c`：中断桥接；只能做Driver应答、快速关波、搬运和投递事件。
- 其余业务模块不得调用`drv_*`。
- APP不得包含`board_config.h`、G32/DDL/CMSIS目标寄存器头。

### Driver

- `driver/inc/driver.h`是统一入口；`drv_*.h`按硬件功能拆分。
- `driver/inc/board_config.h`保存PinMap、ADC/PWM/Flash/PVD/WDT和人工门禁。
- `driver/src/*.c`允许使用Vendor库，禁止包含APP头或调用APP函数。

## 3. 代码规范

函数统一：

```c
/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 函数职责、执行顺序、关键门控和副作用。
 *---------------------------------------------------------------------------*/
```

要求：

- 4空格缩进；禁止Tab、行尾空白；
- 文件级`static`变量/函数/声明集中在首个公开函数前；
- 宏先定义再使用，并写用途和单位；
- 复杂判断解释原因和安全动作；
- 禁止动态内存；
- 不在函数体新增魔法控制参数。

## 4. 修改参数前

充电/保护：读`docs/22`、`docs/23`、`docs/26`。

PVD/弱光启动：读`docs/28`、`docs/29`。

架构修改：读`docs/30`、`docs/31`。

记录：旧值、新值、单位、依据、测试和回退值。

## 5. MCU供电资格

必须：

```text
System时基
→ GPIO安全态
→ PVD Ready
→ VDD高于VPVD并连续稳定100ms候选
→ 才运行完整初始化
```

禁止：

- PVD系统复位；
- PVD IRQ弱光保护；
- MCU低压软件Fault；
- PVD通过前启动IWDT/PWM/COMP/ADC/UART。

## 6. Relay硬约束

```text
Relay OFF
→ Boost充BST_U
→ |BST_U-BAT_U|<=1.5V连续1s
→ PWM OFF
→ app/main.c实时再次复核
→ Relay ON
→ 100ms后压差<=2.5V
→ PWM仍OFF
→ BAT_U 10s max-min<=2V
→ RUN
```

任何反向顺序都属于安全回归。

## 7. PWM红线

- 业务模块不写PWM；
- ISR故障第一动作关PWM；
- CCR preload + 自然UEV；运行期禁止软件UG；
- 第一次发波前先0CCR握手；
- epoch、Protection、实时Break、Break锁存、功率总门全部通过后才arm；
- Automatic Output关闭。

## 8. Debug

`AURORA_DEBUG_ENABLE`默认0。当前`debug.c`只是统一无副作用接口，不绑定产品UART。未来要启用PB7/PB8 Debug UART，应增加对应Driver能力，而不是在业务代码直接操作寄存器。

## 9. Keil

打开：

```text
project/AuroraControl.uvprojx
```

使用ARM Compiler 6。构建后必须检查MAP中：

- 应用镜像不越过`0xFC00`；
- RAM不超过8KB；
- A/B Flash Journal仍为`0xFC00/0xFE00`；
- 无意外Service/Board旧对象。

## 10. 提交前

```bash
python tools/run_checks.py
git diff --check
git status --short
```

如果没有真实Keil/台架证据，提交说明必须明确“软件门禁通过，目标硬件未验收”，且 `BOARD_POWER_OUTPUT_ALLOWED`必须保持0。
