# 28 · REF · G32F031 MCU供电稳定启动与PVD设计依据

## 1. 修改背景

本项目由PV侧辅助电源给MCU供电。弱光、清晨或云层快速遮挡时，PV可提供的电压/电流很小，辅助电源可能处在“能让MCU越过POR开始执行、但3.3V仍未真正稳定”的临界区。

项目历史中曾在其他MCU平台遇到类似问题：弱光导致供电反复爬升/跌落，MCU刚启动便开始完整初始化，新增负载后电压再次下降，最终形成反复复位或长期无法正常启动。

v0.8.0之前，G32F031启动路径为：

```text
Reset
→ drv_system_init()
→ drv_io_init()
→ IWDT
→ PWM
→ COMP
→ ADC
→ UART
→ APP / Flash
→ ADC DMA Start
```

这个顺序缺少“MCU自身VDD已经稳定”的资格判断。虽然功率输出还有软件总门，但IWDT、PWM外设、COMP、ADC、UART等会较早进入初始化阶段。

v0.8.1的目标不是新增一个“弱光故障”，而是恢复成熟120W工程中“电源稳定后才继续初始化”的设计思想，并使用G32F031自己的PVD能力实现。

---

## 2. G32F031手册依据

本设计依据以下资料：

- 《G32F031_M3122_M3114_M3115 数据手册 V1.3》
- 《G32F031_M3122_M3114_M3115 用户手册 V1.3》

### 2.1 POR/PDR只能保证极低电压复位，不能代表3.3V已经稳定

数据手册“7.6.2 内嵌复位和电源控制模块特性测试”给出：

- POR/PDR下降沿：约1.542~1.686V，典型1.625V；
- POR/PDR上升沿：约1.594~1.738V，典型1.676V。

POR/PDR始终工作，这是MCU最基本的Brown-out安全机制，v0.8.1不会关闭、绕过或替代它。

但对于名义3.3V供电，这个门限太低：MCU可能已经退出POR/PDR复位，却并不意味着ADC、COMP、Flash、PWM和外围负载所需的供电条件已经稳定。

### 2.2 G32F031支持可编程PVD

数据手册“表34 可编程电压检测器特性”给出8个PVD档位。与本项目最相关的两档为：

| PVD档位 | 上升典型 | 上升范围 | 下降典型 | 下降范围 |
|---|---:|---:|---:|---:|
| 2.8V档 | 2.80V | 2.74~2.87V | 2.70V | 2.64~2.77V |
| 3.2V档 | 3.20V | 3.13~3.28V | 3.10V | 3.03~3.18V |

PVD带有大约100mV迟滞，可以避免门限附近的高频来回翻转。

### 2.3 PVD有独立Ready和Status

用户手册“6.4.3 电源电压监控”和“6.6.5 PVD控制/状态寄存器”明确给出：

- `PVDRDYFLG`：PVD功能是否已经完成建立；
- `PVDSTS`：VDD当前高于还是低于所选PVD门限；
- PVD刚开启约30us处于建立时间内，此时不能直接相信状态结果。

因此v0.8.1严格区分：

```text
PVD模块未Ready
≠
VDD低于门限
```

前者表示PVD模块初始化异常；后者只是弱光/辅助电源不足，需要继续等待。

### 2.4 G32F031还支持PVD中断和PVD系统复位，但本项目明确不用

用户手册给出：

- `PVDIEN`：PVD监测事件中断；
- `RCC_RSTCSR.PVDRSTEN`：VDD低于PVD阈值时触发系统复位。

对于PV弱光启动，本项目禁止这两种做法。

如果把PVD直接配置成系统复位源，弱光时很容易形成：

```text
VDD刚超过门限
→ MCU启动
→ 负载变化
→ VDD跌破门限
→ PVD Reset
→ 再启动
→ 再Reset
```

这与本次要解决的问题方向相反。

所以v0.8.1定义两个编译期硬约束：

```c
BOARD_MCU_PVD_RESET_ENABLE = 0
BOARD_MCU_PVD_IRQ_ENABLE   = 0
```

并在目标Driver中用`#error`防止后续重构把它们误改为1。

### 2.5 为什么初始候选选2.8V，而不是3.2V

当前板级MCU名义供电约3.3V。

3.2V PVD档的上升最坏值可到3.28V，已经非常接近3.3V名义供电。如果LDO输出存在负偏差、温度漂移或弱光下压降，可能出现“MCU供电其实已经可以正常工作，但始终过不了3.2V档最坏门限”的反向死锁风险。

2.8V档上升范围2.74~2.87V，给3.3V供电留有更合理裕量。

同时数据手册给出：

- ADC标准电压工作区最低约2.7V；
- COMP模拟电源最低约2.7V。

因此2.8V作为第一次台架候选具有工程合理性，但它仍不是量产冻结值，最终要结合VDD实测波形、LDO容差和温度测试确定。

---

## 3. v0.8.1启动策略

### 3.1 核心原则

```text
供电不足 ≠ 软件Fault
供电不足 = 保持安全态并继续等待
```

PVD只用于决定：

> 是否允许进入完整外设和业务初始化。

PVD不参与：

- PV弱光产品保护；
- MPPT运行保护；
- Fault bit；
- 30s故障恢复；
- 运行期自动复位。

### 3.2 完整启动顺序

```text
Reset / POR-PDR释放
        ↓
SystemCoreClock / SysTick
        ↓
最小GPIO安全态
  GLC   = LOW
  GHC   = LOW
  RELAY = OFF
  LINK  = OFF
        ↓
配置PVD
  Threshold = BOARD_MCU_PVD_THRESHOLD_MV
  Filter    = BOARD_MCU_PVD_FILTER_US
  IRQ       = OFF
  PVD Reset = OFF
        ↓
等待PVDRDY
        ↓
读取PVDSTS
        ↓
VDD高于门限？
  NO → 连续稳定时间清0 → WFI等待下一1ms节拍
  YES
        ↓
连续稳定达到BOARD_MCU_SUPPLY_STABLE_TIME_MS？
  NO → 继续观察；期间一旦跌破门限立即重新计时
  YES
        ↓
关闭PVD
        ↓
进入原Service完整初始化
  IWDT
  PWM
  COMP
  ADC
  UART
  APP / Flash
  ADC DMA Start
```

### 3.3 为什么供电资格阶段不启动IWDT

弱光时“长时间等待”本身是预期行为，不是程序卡死。

如果IWDT提前开启，会把正常弱光等待变成：

```text
等待供电稳定
→ IWDT超时
→ Reset
→ 再等待
→ 再Reset
```

因此IWDT必须在供电资格通过后由原Service健康监督启动。

### 3.4 为什么仍然先初始化最小GPIO安全态

在PVD等待之前，只有功率相关GPIO需要进入确定安全态：

- PA15 / GLC低；
- PA14 / GHC低；
- PA13 / RELAY断开；
- PA12 / LINK关闭。

这一步沿用现有`drv_io_init()`，目的是防止PVD等待期间功率控制脚处于不确定状态。

PWM复用、COMP、ADC、UART等仍在供电资格通过后才初始化。

---

## 4. 可调宏与默认值

参数集中在`board/board_config.h`，禁止在函数体内写魔法数。

| 宏 | v0.8.1默认值 | 作用 |
|---|---:|---|
| `BOARD_MCU_PVD_THRESHOLD_MV` | 2800mV | PVD名义门限 |
| `BOARD_MCU_PVD_FILTER_US` | 50us | PVD数字滤波长度，当前按64MHz启动时钟映射 |
| `BOARD_MCU_PVD_READY_TIMEOUT_US` | 1000us | 等待PVDRDY最大时间；超时视为PVD模块异常 |
| `BOARD_MCU_SUPPLY_STABLE_TIME_MS` | 100ms | VDD连续高于门限后还需保持的时间 |
| `BOARD_MCU_SUPPLY_CHECK_PERIOD_MS` | 1ms | 供电资格轮询周期 |
| `BOARD_MCU_PVD_RESET_ENABLE` | 0 | 禁止PVD系统复位 |
| `BOARD_MCU_PVD_IRQ_ENABLE` | 0 | 禁止PVD中断保护 |

其中100ms继承旧120W成熟工程“电源稳定后再继续初始化”的设计思路；2.8V、50us和具体台架余量仍需新板验证。

---

## 5. 与旧120W工程的关系

旧120W工程已经存在LVD启动资格思想：先初始化LVD，等待电源恢复，再连续稳定一段时间；稳定期间如果LVD再次触发，则稳定计时重新从0开始。

v0.8.1不照搬HT32寄存器或`LVD.c`，只迁移成熟行为：

```text
旧120W：LVD状态 → 连续稳定 → 继续初始化
新300W：G32F031 PVD状态 → 连续稳定 → 继续初始化
```

两者硬件接口不同，但产品目标一致。

---

## 6. 弱光典型行为

例如VDD在门限附近抖动：

```text
0ms    高于VPVD
45ms   低于VPVD → stable=0
80ms   高于VPVD
150ms  低于VPVD → stable=0
300ms  高于VPVD
400ms  仍高于VPVD → PASS
```

这里必须是“连续100ms”，不能累计多个不连续片段。

弱光可以持续数分钟甚至更久；只要PVD模块本身已经Ready，系统就保持安全态等待，不进入Fault，也不要求人工重新上电。

太阳最终增强、VDD稳定后，应自动继续完整初始化。

---

## 7. 台架验证要求

正式冻结VPVD前至少观察：

```text
CH1 = MCU VDD
CH2 = NRST
CH3 = PA15 / GLC
CH4 = PA13 / RELAY
```

测试场景：

1. VDD非常缓慢上升；
2. VDD在2.7~3.3V附近反复摆动；
3. VDD超过门限后短暂跌落再恢复；
4. 比较2.8V档和3.2V档在最低输入、最高负载、冷热环境下的启动裕量。

验收：

- VDD没有连续稳定时，GLC始终低，Relay始终断开；
- 弱光等待不产生PVD Fault，不产生PVD Reset循环；
- 最终VDD稳定后，无需重新插拔即可继续初始化；
- 供电资格阶段IWDT不启动；
- 通过后原v0.8.0“先Boost充BST_U、压差接近后才吸Relay”的安全链保持不变；
- `BOARD_POWER_OUTPUT_ALLOWED`在所有台架证据完成人工审核前继续保持0。

---

## 8. 结论

v0.8.1的PVD不是新增保护功能，而是一个**启动资格门**：

> MCU只有在自身VDD连续稳定后，才允许进入完整固件初始化。

这样既避免弱光时过早拉起外设造成反复启动，也避免把正常弱光等待错误处理成Fault或系统复位。
