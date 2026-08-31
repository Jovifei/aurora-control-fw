# 工程接手与修改指南

## 1. 第一次接手

先执行：

```bash
python tools/run_checks.py
```

正常结果应包含：

```text
ARCHITECTURE CHECK: PASS
CODE STYLE CHECK: PASS
GCC strict build + CTest: PASS
Clang strict build + CTest: PASS
Clang ASan/UBSan: PASS
Cortex-M0+ target syntax: PASS
```

Host测试通过不等于Keil链接和功率板验收通过。

如果本机缺少GCC、Clang、CMake或CTest，`tools/run_checks.py`会明确输出`CHECKS INCOMPLETE`并返回非0；这表示Host构建证据缺失，不是测试通过。

## 2. 源码入口

```text
app/inc/       应用层公共类型、接口、带单位参数和运行时接口
app/src/       应用业务、目标入口、ISR桥接和Debug实现
driver/inc/    驱动模块头、公共契约、PinMap和板级安全配置
driver/src/    G32F031目标外设及板级驱动实现
project/      AC6工程、scatter和用户工程配置
tests/         Host回归、故障注入和模拟驱动
tools/         架构、风格、编译、Sanitizer和目标端语法门禁
```

应用层文件约定：

- `.h`只能放入 `app/inc/`；
- `.c`只能放入 `app/src/`；
- `app/`根目录不得放 `.c/.h`；
- APP可包含Driver契约并调用驱动；不得直接包含任何G32/DDL/CMSIS芯片头文件。

## 3. 代码注释与布局规范

所有函数定义必须使用：

```c
/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 函数职责、执行顺序、关键门控和副作用。
 *---------------------------------------------------------------------------*/
```

此外：

- 条件判断前解释“为什么判断”，执行分支内解释“安全动作或状态变化”；
- 不为显而易见的赋值写重复注释；
- 不使用Tab；采用4空格缩进；
- 所有文件级 `static` 变量、常量、原型和函数定义集中在首个公开函数之前；
- 文件级宏必须先定义、再使用，并注明单位和用途；
- 占空比、ADC码、CCR和物理量不得混用同一命名。

`tools/check_code_style.py`会自动拒绝缺少统一函数头、Tab、行尾空白、迟到的 `static/#define` 和错误APP目录。

## 4. 参数修改流程

1. 先查 [17-参数标定与Codex交接清单](17-参数标定与Codex交接清单.md)。
2. 明确参数属于：应用算法、板级标定、目标外设或人工门禁。
3. 记录单位、旧值、新值、依据、测试方法和回退值。
4. 修改对应集中定义，不在函数体内追加魔法数。
5. 增加或修改Host测试。
6. 运行完整门禁。
7. 若涉及PWM、COMP、ADC、Flash或IWDT，再完成Keil与板级测试；不能只凭Host结果解锁。

## 5. 功率安全红线

- 正常发波只能经过 `app/src/main.c::apply_power_command()`；
- 快速故障ISR第一动作必须是 `drv_pwm_force_off_isr()`；
- 运行期Duty只写CCR preload，不产生软件UPDATE事件；
- 首次授权必须先装载零CCR，再经过安全epoch、Break源、Break锁存和板级总门复核；
- 故障后Duty和积分清零，恢复必须重新执行电池识别与预充；
- 功率运行或继电器闭合时禁止Flash擦写；
- 只有应用健康监督可喂IWDT；
- `BOARD_POWER_OUTPUT_ALLOWED`未经人工验收不得改为1。

## 6. 提交前检查

```bash
python tools/run_checks.py
git diff --check
git status --short
```

Keil/台架证据未完成时，提交说明必须明确写“Host验证通过，Keil链接/板级验证未执行”，不得使用“量产通过”“硬件安全闭环”等表述。

## 7. 蓝牙与Debug路由

工程默认由PA10/PA11承载蓝牙USART。需要观察`[GE_DEBUG]`日志时，在构建宏中选择：

```text
BOARD_USART_MODE=BOARD_USART_MODE_DEBUG
DEBUG_ENABLE=1
```

该模式把USART切换到PB7/PB8，并关闭产品协议解析和主动遥测；不要在蓝牙模式打开Debug打印，否则会污染蓝牙数据。打印实现位于`app/src/debug.c`，各模块开关位于`app/inc/debug.h`。

## 8. MOS温度状态

PB12的板载MOS NTC已按原理图R37=5.1K、R42=100K 1% 3950实现-40°C～125°C查表换算。开路、短路或超出换算范围会锁存MOS温度传感器故障，必须按保护流程显式清除。PB5接CON4外接环境NTC，待实际探头型号和B值确认后再启用。

## 9. MCU弱光供电资格

目标入口必须先建立最小安全GPIO，再调用 `drv_system_wait_for_supply_stable()`；只有PVD Ready、VDD高于2.8V候选门限并连续稳定100ms后，才能进入 `aurora_runtime_init()` 初始化IWDT、PWM、COMP、ADC和UART。`BOARD_MCU_PVD_RESET_ENABLE` 与 `BOARD_MCU_PVD_IRQ_ENABLE` 必须保持0；弱光供电不足只等待。


## 当前接手与审计

首次接手或从旧远端覆盖时，先阅读 [18-v0.7.2目录规范与交接说明](18-v0.7.2目录规范与交接说明.md) 和 [19-编译修复提交2740523审计](19-编译修复提交2740523审计.md)。

## 10. v0.10.2 阅读入口

准备阅读或修改当前候选代码时，依次查看：

1. [程序代码分析阅读索引](程序代码分析/00-阅读索引.md)
2. [v0.10.2成熟行为二次审计与实现说明](41-v0.10.2-成熟行为二次审计与实现说明.md)
3. [Demo无电池带载模式与安全边界](42-v0.10.2-Demo无电池带载模式与安全边界.md)
4. [Keil ARMCLANG修复与发布边界](43-v0.10.2-Keil_AC6修复与发布边界.md)
