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

## 2. 源码入口

```text
app/inc/       应用层公共类型、接口和带单位参数
app/src/       9个纯业务实现
service/       ISR事件邮箱、主循环调度、APP与Driver唯一桥接
board/         PinMap、标定、Flash地址和人工安全门禁
driver/        G32F031目标外设实现
project/keil/  AC6工程、main、中断和scatter
tests/         Host回归、故障注入和模拟驱动
tools/         架构、风格、编译、Sanitizer和目标端语法门禁
```

应用层文件约定：

- `.h`只能放入 `app/inc/`；
- `.c`只能放入 `app/src/`；
- `app/`根目录不得放 `.c/.h`；
- APP不得包含 `board.h`、`driver.h`、`service.h` 或任何G32/DDL/CMSIS目标头文件。

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

- 正常发波只能经过 `service/service.c::apply_power_command()`；
- 快速故障ISR第一动作必须是 `drv_pwm_force_off_isr()`；
- 运行期Duty只写CCR preload，不产生软件UPDATE事件；
- 首次授权必须先装载零CCR，再经过安全epoch、Break源、Break锁存和板级总门复核；
- 故障后Duty和积分清零，恢复必须重新执行电池识别与预充；
- 功率运行或继电器闭合时禁止Flash擦写；
- 只有Service健康监督可喂IWDT；
- `BOARD_POWER_OUTPUT_ALLOWED`未经人工验收不得改为1。

## 6. 提交前检查

```bash
python tools/run_checks.py
git diff --check
git status --short
```

Keil/台架证据未完成时，提交说明必须明确写“Host验证通过，Keil链接/板级验证未执行”，不得使用“量产通过”“硬件安全闭环”等表述。


## 当前替换发布

首次接手或从旧远端覆盖时，先阅读 [20-v0.7.1替换发布说明](20-v0.7.1替换发布说明.md)。
