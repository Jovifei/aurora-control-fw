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
Python contract tests: PASS
GCC strict build + CTest: PASS
Clang strict build + CTest: PASS
Clang ASan/UBSan: PASS
Cortex-M0+ target syntax: PASS
```

Host测试通过不等于Keil链接和功率板验收通过。

## 2. 源码入口

```text
app/inc/          应用层公共类型、接口和带单位参数
app/src/          measurement / charger / protection / mppt / power_stage等业务实现
service/          ISR事件邮箱、主循环调度、APP与Driver唯一桥接
driver/inc/       目标驱动统一接口
driver/src/       G32F031目标外设实现
board/            PinMap、标定、Flash地址和人工安全门禁
project/keil/     AC6工程、main、中断和scatter
tests/            Host回归、故障注入和模拟驱动
tools/            架构、风格、编译、Sanitizer和目标端语法门禁
```

应用层文件约定：

- `.h`只能放入 `app/inc/`；
- `.c`只能放入 `app/src/`；
- `app/`根目录不得放 `.c/.h`；
- APP不得包含 `board.h`、`driver.h`、`service.h` 或任何G32/DDL/CMSIS目标头文件。

驱动层同样采用 `driver/inc/*.h` 与 `driver/src/*.c`，不得反向包含APP业务头文件。

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
2. 涉及120W成熟行为时，同时核对 [22-REF-120W-V2.7行为与参数基线](22-REF-120W-V2.7行为与参数基线.md) 和 `reference/README.md` 指向的原始V2.7来源。
3. 涉及MCU供电、PVD、弱光启动时，同时查 [28-REF-G32F031-MCU供电稳定启动与PVD设计依据](28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md)。
4. 明确参数属于：产品公共行为、300/120功率BOM、板级标定、目标外设或人工门禁。
5. 记录单位、旧值、新值、依据、测试方法和回退值。
6. 修改对应集中定义，不在函数体内追加魔法数。
7. 增加或修改Host测试。
8. 运行完整门禁。
9. 若涉及PWM、COMP、ADC、Flash、Relay、IWDT或PVD，再完成Keil与板级测试；不能只凭Host结果解锁。

## 5. 功率安全红线

- 正常发波只能经过 `service/service.c::apply_power_command()`；
- 快速故障ISR第一动作必须是 `drv_pwm_force_off_isr()`；
- 运行期Duty只写CCR preload，不产生软件UPDATE事件；
- 首次授权必须先装载零CCR，再经过安全epoch、Break源、Break锁存和板级总门复核；
- 故障后Duty和积分清零，恢复必须重新执行启动与预充；
- 功率运行或继电器闭合时禁止Flash擦写；
- 只有Service健康监督可喂IWDT；
- `BOARD_POWER_OUTPUT_ALLOWED`未经人工验收不得改为1。

### Relay硬约束

必须严格按下面顺序：

```text
Relay OFF
→ 受限Boost先给BST_U充电
→ |BST_U-BAT_U| <= 1.5V连续1s
→ Duty归0，Service确认物理PWM关闭
→ Service用最新BST_U/BAT_U、故障和PWM状态再次复核
→ Relay ON
→ 100ms机械稳定并复核压差<=2.5V
→ PWM继续OFF，观察BAT_U完整10s且max-min<=2V
→ RUN / MPPT
```

任何“先吸合继电器，再让Boost把BST_U抬到BAT_U”的实现都禁止进入评审。

## 6. 提交前检查

```bash
python tools/run_checks.py
git diff --check
git status --short
```

Keil/台架证据未完成时，提交说明必须明确写“Host验证通过，Keil链接/板级验证未执行”，不得使用“量产通过”“硬件安全闭环”等表述。

## 7. 当前基线与历史审计

接手当前工程先读：

- [18-v0.7.2目录规范与交接说明](18-v0.7.2目录规范与交接说明.md)
- [19-编译修复提交2740523审计](19-编译修复提交2740523审计.md)

旧v0.7.0/v0.7.1一次性替换文档不再作为当前操作入口。

## 8. v0.8.0 接手必读

开始修改充电、保护、启动、Relay或MPPT前，必须先读：

- `22-REF-120W-V2.7行为与参数基线.md`
- `23-120W到300W行为迁移矩阵.md`
- `24-300W重构工程介绍.md`
- `25-REF-300W重构录音工程摘要.md`
- `26-v0.8.0参数待确认与台架清单.md`
- `27-v0.8.0实现与验证报告.md`

特别禁止：

1. 把120W功率/电流阈值按300/120机械放大；
2. 把BAT_I_EST写成真实测量；
3. 重新引入同步MOS、互补PWM或旧HT32闭源MPPT库；
4. 在BST_U尚未接近BAT_U时吸合继电器；
5. 绕过Service直接开启PWM/MOE；
6. 在`BOARD_POWER_OUTPUT_ALLOWED == 0`时声称已具备额定功率运行条件。

## 9. v0.8.1 MCU供电资格与弱光启动

v0.8.1新增的不是“MCU弱光保护”，而是完整初始化之前的供电资格门。先读：

- `28-REF-G32F031-MCU供电稳定启动与PVD设计依据.md`
- `29-v0.8.1-MCU供电资格修改与验证报告.md`

启动顺序必须保持：

```text
Reset / POR-PDR
→ SystemCoreClock + 1ms SysTick
→ drv_io_init()建立GLC/GHC/Relay/Link安全态
→ PVD配置并等待PVDRDY
→ VDD连续高于VPVD达到BOARD_MCU_SUPPLY_STABLE_TIME_MS
→ 关闭PVD
→ aurora_service_init()
→ IWDT / PWM / COMP / ADC / UART / APP / Flash
```

规则：

1. `PVDSTS=LOW`表示继续等待，不产生`AURORA_FAULT_*`；
2. VDD中途跌破VPVD，连续稳定时间必须清零，不能累计多个不连续时间片；
3. PVD资格阶段不启动IWDT，弱光等待多久都不是看门狗故障；
4. 禁止`PVDRSTEN`和PVD中断参与本项目弱光逻辑；
5. PVD模块`PVDRDY`自身超时才属于启动硬件异常；
6. 供电资格通过后关闭PVD，正常运行弱光仍由PV_U、PV功率和原v0.8.0状态机处理；
7. VPVD、PVD滤波、PVD Ready超时、连续稳定时间必须只改`board/board_config.h`中的宏；
8. 默认2.8V/100ms只是当前工程候选，最终必须用VDD/NRST/GLC/Relay实测冻结。
