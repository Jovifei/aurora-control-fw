# G00 — PinMap 与 BOM 冻结

- **Gate**: G0
- **状态**: IN_PROGRESS（文档已出，待 Jovi 签字确认）
- **日期**: 2026-09-01
- **源工程**: `d:\work\aurora-control-fw`（v0.10.3）
- **目标工程**: `D:\work\mppt-charger-300w\Application`
- **权威依据**: G32F031 数据手册（表格 8 端口A 复用功能、表格 9 端口B 复用功能、表格 10 `GPIOA_AF3RMP`、表格 34 PVD 特性）、G32F031 用户手册（`PMU_PVDCSR`）、最终原理图基线 2026-08-26

---

> **编号说明**：本文按主题分节，不再使用 G0-x 标签（原 §2~§5 的 G0-2/G0-4/G0-5/G0-6 与任务清单编号冲突，2026-09-01 移除）；任务清单的 G0-x 编号见 `tasks/todo.md`「G0 冻结」小节。

## 1. 冻结后的 PinMap（冻结记录 = 本表；代码侧 = 目标工程各驱动头）

> 原「唯一真源 `driver/inc/board_config.h`」已随代码风格重构删除：PinMap 宏按模块分散到目标工程 `driver/inc/drv_io.h`（数字脚）、`drv_adc.h`（模拟通道）、`drv_debug_uart.h`（调试串口）、`drv_device.h`（产品 USART）。本表仍是冻结依据；代码侧宏与本表冲突时，以本表为准并回改代码。

| 引脚 | 功能 | 模式 | AF | 数据手册依据 | 备注 |
|---|---|---|---|---|---|
| PA15 | GLC 低侧 Boost 门极 | ATMR_CH0 输出 | AF3 | 表格 8 / 表格 10 bit31:28 `000/110/111 = ATMR_CH0（默认）` | **唯一主 PWM** |
| PA14 | GHC | **GPIO 推挽输出，恒定低** | — | 表格 8 PA14 AF3 默认为 `ATMR_CH0N`（表格 10 bit26:24 `011`） | 原理图有网络但**高侧 MOS 未装配**；必须保持 GPIO LOW，禁止配成 CH0N |
| PA13 | RELAY 继电器控制 | GPIO 推挽输出 | — | — | 高有效，默认断开 |
| PA12 | LINK 控制 | GPIO 推挽输出 | — | — | 默认关闭；**禁止作示波器探针** |
| PA10 | 产品 USART_TX | AF0 | AF0 | 表格 8 PA10 = `USART_TX` | `USART`@0x40003800，产品/蓝牙协议用，G2 不初始化 |
| PA11 | 产品 USART_RX | AF0 | AF0 | 表格 8 PA11 = `USART_RX` | 同上 |
| **PB7** | **Debug UART_TX** | AF0 | AF0 | **表格 9 PB7 AF0 = `UART_TX`** | `UART`@0x40003C00，`UART_IRQn = 28` |
| **PB8** | **Debug UART_RX** | AF0 | AF0 | **表格 9 PB8 AF0 = `UART_RX`** | 同上 |
| PB9 | LED_RUN | GPIO 推挽输出 | — | — | **低电平点亮**，启动先灭（输出高） |
| PB10 | COMP0_OUT → U6 EN | AF7 | AF7 | 表格 9 PB10 AF7 = `COMP0_OUT` | U6 EN 高有效 → 故障时必须拉低 |
| PB11 | LED_FAULT | GPIO 推挽输出 | — | — | 低电平点亮；G2 期间兼作 1 ms 节拍探针 |
| PA7 | ADC_IN0 / COMP0_INP | 模拟 | — | 表格 8 PA7 = `ADC_IN0` | |
| PA8 | ADC_IN1 | 模拟 | — | 表格 8 PA8 = `ADC_IN1` | |
| PA9 | ADC_IN2 | 模拟 | — | 表格 8 PA9 = `ADC_IN2` | |
| PB0 | ADC_IN3 | 模拟 | — | 表格 9 PB0 Analog = `ADC_IN3` | |
| PB1 | ADC_IN4 | 模拟 | — | 表格 9 PB1 Analog = `ADC_IN4, COMP1_INN, VREF+` | **PB1 没有 COMP2_INN**（COMP2_INN 在 PB2） |
| PB5 | ADC_IN6 | 模拟 | — | 表格 9 PB5 Analog = `ADC_IN6, COMP3_INN` | |
| PB12 | ADC_IN5 | 模拟 | — | 表格 9 PB12 Analog = `ADC_IN5` | |
| PA0/PA1/PA2 | OPAMP0 OUT/INN/INP | 模拟 | — | 表格 8 | PV_I 前置 ×16 |

### 1.1 PB7/PB8 选择的成立性论证

官方 SDK 示例 `g32f031_uart_cfg.c` 把独立 `UART` 外设映射到 **PB1/PB2**，但本板 PB1 = `ADC_IN4`（BST_U）、PB2 = `COMP2_INN`，均已占用。
数据手册表格 9 证明 **PB7/PB8 的 AF0 就是同一个 `UART` 外设的 TX/RX**，且这两脚在本板上完全空闲 —— 因此 PB7/PB8 是唯一可用的独立调试串口引脚对。

> **反驳记录**：外部方案称「G32 只有一个 USART，Debug 必须与 `BSP_OTA` 共用 PA10/PA11」——**事实错误**。
> `g32f031xx.h` 中 `USART_IRQn = 27`、`UART_IRQn = 28` 两个向量并存，startup 文件两个 weak Handler 都在。

---

## 2. ADC 通道映射冲突裁决

两工程原本的映射**互不兼容**，逐字对照如下：

| ADC 逻辑索引 | aurora `board_config.h`（**采纳**） | 目标原 `app_hw_config.h`（废弃） |
|---|---|---|
| 0 | `PV_I`，物理通道 1 | `PA8 / PV_U` |
| 1 | `PV_U`，物理通道 2 | `PA9 / BAT_U` |
| 2 | `BAT_U`，物理通道 3 | `PB0 / BST_U` |
| 3 | `BUS_U`(BST_U)，物理通道 4 | `PB12 / NTC_MOS` |
| 4 | `NTC_MOS`，物理通道 5 | `PB5 / NTC_AMB` |
| 5 | `NTC_AMB`，物理通道 6 | （不存在，仅 5 通道） |

**裁决（Jovi 已决策）**：采用 aurora 的 **6 通道含 PV_I** 映射。目标原 5 通道映射作废；`app_hw_config.h` 已在代码风格重构时删除，旧映射不得再引用。

> **纠错记录**：外部方案的基线对照表写「现 Application：PA8=PV_U、PB0=BST_U、**PB1=COMP2−**」。
> 前两项正确；**`PB1=COMP2−` 是错的** —— 目标 `app_hw_config.h` 里根本没有这一行，且数据手册表格 9 中 PB1 的模拟功能是 `ADC_IN4 / COMP1_INN / VREF+`，**`COMP2_INN` 在 PB2**。此错误若被抄入 G0 会直接污染冻结基线。

---

## 3. 拓扑红线（写入代码注释，每批复查）

1. **单路异步 Boost**，PA15/GLC 是唯一主 PWM
2. **禁止**互补 PWM / 死区 / DCM-CCM 同步整流（120W 时代的遗留概念，本板无高侧 MOS）
3. PA14/GHC 永久 GPIO 推挽输出低
4. 功率 GPIO 安全态必须在任何定时器/比较器初始化**之前**建立
5. `DRV_DEVICE_GATE_*` / `DRV_DEVICE_POWER_OUTPUT_ALLOWED` 是**最终验收证据门**，不是 Bring-up 使能开关

---

## 4. BOM 关键项

| 项 | 值 | 影响 |
|---|---|---|
| MCU | Geehy G32F031K8T6，LQFP32，Cortex-M0+，64 MHz HSI，64 KB Flash，8 KB SRAM | SRAM 预算在 G4 复核 |
| LDO | HT75R33-1，输出 3.3 V | 同时作为 ADC 参考 `DRV_ADC_REFERENCE_MV = 3300` |
| PV_I 采样 | 3 mΩ 分流器 + 内部 OPA ×16，VCM = AVDD/2 | 0 A ≈ code 2048，≈16.79 mA/code |
| PV_U 分压 | 75k / 3k，比例 26 | ≈20.95 mV/code |
| BAT_U 分压 | 15M / 510k，比例 15510/510 ≈ 30.41 | ≈24.51 mV/code，高源阻抗需建立时间专项（G3-4） |
| BST_U 分压 | 125k / 5k，比例 26 | 理论满量程仅 ≈85.8 V，**72V 高 SOC 档 87~93 V 超量程** |
| NTC ×2 | 5.1k 上拉 + 100K/B3950 | 两路同特性，25 °C 理论 raw ≈3896 |
| U6 EN | 高有效 | COMP0 故障输出必须拉低（`DRV_COMP0_FAULT_ACTIVE_LOW = 1`） |
| LED ×2 | 低电平点亮 | `DRV_IO_LED_ACTIVE_LOW = 1` |

---

## 5. `LOCKUPRSTEN` 与 HardFault 策略决策

`RCC->RSTCSR` 提供 `LOCKUPRSTEN`（bit15）：使能后 Cortex-M0+ 进入 LOCKUP 状态时自动触发系统复位；对应标志 `LOCKUPRSTFLG`（bit6）。

**决策：G0~G5 期间 `LOCKUPRSTEN` 保持复位默认值 0（不使能）。**

理由：

1. Bring-up 阶段需要 HardFault **停在原地**以便调试器 attach 看现场；自动复位会毁掉现场
2. G1 的 `HardFault_Handler` 策略是「**先强制功率 GPIO 安全态，再停机自旋**」—— 停机前已完成安全动作，无需硬件复位兜底
3. `LOCKUPRSTEN` 的启用时机推迟到 **G13 及以后的整机可靠性阶段**，届时与 IWDT 复位策略一起评审

**配套约定**：G2-C4 的启动日志必须解析并打印 `LOCKUPRSTFLG`。若该位在 `LOCKUPRSTEN = 0` 的前提下仍被置位，说明有人改过配置，属于配置漂移告警。

---

## 6. 中断优先级冻结表

Cortex-M0+ 只有 2 位优先级（0~3），无优先级分组寄存器；数值越小优先级越高。
对尚未使能的中断预设优先级是无副作用的纯寄存器写。落点：`drv_irq_configure_priorities()`（目标工程 `driver/src/drv_system.c`）。

| IRQ | 优先级 | 用途 |
|---|---|---|
| `COMP0_IRQn` | 0 | 硬件过流关断链（COMP0 → PB10 → U6 EN） |
| `COMP1_2_3_IRQn` | 0 | COMP2 边沿事件计数 |
| `ATMR_BRK_UP_TRG_COM_IRQn` | 0 | ATMR Break（COMP0 硬件刹车） |
| `DMA_CH1_IRQn` | 1 | G4 起 ADC DMA 搬运 |
| `ADC_IRQn` | 2 | 规则组 EOS 中断 |
| `SysTick_IRQn` | 2 | 1ms 系统节拍 |
| `USART_IRQn` | 3 | 产品协议（PA10/PA11，OTA/蓝牙共用） |
| `UART_IRQn` | 3 | Debug 串口（PB7/PB8） |

> 快速故障（COMP/ATMR Break）必须抢占 PWM 更新、DMA 和通信；采样与节拍同级；通信最低。
> legacy `drv_comp.c` 的 `DRV_COMP_Init` 内部把 COMP0/COMP1_2_3 设为 1，与本表不一致——
> 属两代配置共存（该实现当前无调用点，G7 重写时按本表收敛）。

## 7. 证据清单

| 文件 | 内容 | 状态 |
|---|---|---|
| `README.md`（本文） | PinMap 冻结表 + AF 依据 + 通道裁决 + 红线 + BOM + LOCKUPRSTEN 决策 + 中断优先级冻结表 | ✅ |
| `flash-map.md` | Flash 分区冻结与 IROM1 收窄 | ✅ |
| `RESULT.md` | Gate 判定 | ✅ |

## 8. 待 Jovi 确认项

1. ADC 6 通道映射裁决（已在对话中决策，此处仅归档）
2. `LOCKUPRSTEN = 0` 的 Bring-up 期决策
3. `flash-map.md` 中新冻结的参数页地址与 IROM1 尺寸
4. 中断优先级冻结表（2026-09-01 新增 §6，随 PinMap 一并签字）
