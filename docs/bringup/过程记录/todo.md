# G0→G5 移植执行计划（aurora-control-fw → mppt-charger-300w/Application）

- 依据规范：`docs/46-v0.10.3-新工程分阶段移植与板级验证路线.md`
- 源工程：`D:\work\aurora-control-fw`（两层架构 driver/ + app/，AC6）
- 目标工程：`D:\work\mppt-charger-300w\Application`（IAP 应用区，AC5）
- 目标范围：**到 G5 为止**，分 3 批交付
- 已确认决策：ADC 采用 aurora 6 通道（含 PV_I）映射；整体搬 aurora 两层架构；Debug UART 走 PB7/PB8 独立 UART 外设；分 3 批（G0+G1 / G2 / G3+G4+G5）
- 代码修改统一通过 `/code_wrt` skill 执行
- 本版已并入外部方案 `~/.cursor/plans/g0-g5_bring-up_plan_5863fe07.plan.md` 的审议结论，详见 §7

---

## 0. 前置事实核查结论（已用权威资料证实，不是推断）

### 0.1 PB7/PB8 → 独立 UART 外设，AF0（用户选择成立）

证据：`G32F031_M3122_M3114_M3115 数据手册 V1.3.pdf` 表格 9「端口B 复用功能配置」

| 引脚 | AF0 | AF3 | AF4 | AF7 | Analog |
|---|---|---|---|---|---|
| PB0 | - | ATMR_CH3N | GTMR_CH3 | COMP1_OUT | **ADC_IN3** |
| PB1 | UART_TX | ATMR_BKIN | GTMR_CH0_ETR | COMP0_OUT | **ADC_IN4**, VREF+ |
| PB2 | UART_RX | ATMR_ETR | GTMR_CH1 | - | **COMP2_INN** |
| PB5 | UART_TX | - | GTMR_CH2 | COMP2_OUT | **ADC_IN6** |
| PB6 | UART_RX | - | GTMR_CH3 | COMP3_OUT | ADC_IN7, **COMP123_INP** |
| **PB7** | **UART_TX** | - | GTMR_CH1 | COMP1_OUT | - |
| **PB8** | **UART_RX** | - | GTMR_CH0_ETR | - | - |
| PB10 | USART_RX | ATMR_ETR | GTMR_CH0_ETR | **COMP0_OUT** | - |
| PB12 | USART_RTS | ATMR_CH3 | GTMR_CH2 | COMP2_OUT | **ADC_IN5** |

结论与交叉校验：
- **PB7 = UART_TX @ AF0，PB8 = UART_RX @ AF0**，挂在**独立 UART 外设**（`UART_BASE 0x40003C00`，`UART_IRQn = 28`，`DDL_APB_GRP1_PERIPH_UART`，startup 已有 weak `UART_IRQHandler`）。
- 官方 SDK `UART_EchoInterrupt` 例程用 PB1/PB2，那只是同一个 UART 外设的另一组 AF0 引脚；本板 **PB1 已是 BST_U(ADC_IN4)、PB2 已是 COMP2_INN**，不可占用 → PB7/PB8 是唯一正确的空闲对。
- 产品 UART 走 PA10/PA11 = USART_TX/RX @ AF0（`USART_BASE 0x40003800`，`USART_IRQn = 27`），与 Debug UART **物理上完全独立**，两路可共存。
- PB10 AF7 = COMP0_OUT ✅ 与 aurora `drv_comp.c` 的 AF7 一致。
- ADC 物理通道全部对齐 aurora `board_config.h`：PA8=ADC_IN1(PV_I)、PA9=ADC_IN2(PV_U)、PB0=ADC_IN3(BAT_U)、PB1=ADC_IN4(BST_U)、PB12=ADC_IN5(NTC_MOS)、PB5=ADC_IN6(NTC_AMB)。
- PA15 AF3 默认 = **ATMR_CH0** ✅（`GPIOA_AF3RMP` bit31:28 = 000）。
- PA14 AF3 默认 = **ATMR_CH0N** ⚠ → 一旦误配 AF3 就会输出互补波形。**PA14 必须永久保持普通 GPIO 推挽输出低。**

### 0.2 两工程 DDL 库版本不同 → 目标工程保留自己的 DDL，不搬 aurora 的 `vendor/`

逐文件忽略 CRLF 比较后的实质差异（其余头文件仅版本注释差异）：

| 文件 | 差异 | 影响 |
|---|---|---|
| `g32f031_ddl_pmu.h` | **7 个函数改名** | ⚠ G2-A 必改 |
| `g32f031_ddl_rcc.h` | `DDL_RCM_GetSystemClocksFreq`→`DDL_RCC_GetSysctrlClocksFreq`；目标新增 `DDL_RCC_APB_PERIPHERAL_*`/`AHB_PERIPHERAL_*`/`*_RESET` 别名 | aurora 未用到旧名，无影响 |
| `g32f031_ddl_flash.h` | `DDL_FLASH_RPT_KEY_LOCK`→`DDL_FLASH_RDPRT_KEY_LOCK` | G7 才涉及 |
| `g32f031_ddl_atmr.h` + `g32f031xx.h` | **`ATMR_CR1_UDISEN` 在 aurora 是 bit1，在目标是 bit2**；`DDL_ATMR_InitTypeDef` 字段顺序不同（aurora 多一个 `Reserved`） | ⚠⚠ G6 隐性地雷 |
| `g32f031_ddl_uart.h` | `DATAWIDTH_9B` 用 `UART_CR1_M` vs `UART_CR1_M_0`；目标多 `DATAWIDTH_7B` | 我们用 8B，无影响 |
| `g32f031_ddl_gtmr.c` | aurora 是手工裁剪的 192 行 3 函数子集；**目标是官方完整 976 行 11 函数** | 目标版本更完整，保留目标版 |
| `g32f031_ddl_adc.h/.c`、gpio、dma、cortex、utils、iwdt、usart | 仅版本注释 | ✅ G3/G4 移植干净 |

**决策：目标工程 `Libraries/G32F031_DAL_Driver` 与 `Libraries/Device` 一律不动，作为唯一真源；把 aurora 的 `driver/` + `app/` 代码搬过去并适配 API 名。**

需要适配的 aurora 代码只有 5 行（全在 `driver/src/drv_system.c`）：

| aurora 旧名 | 目标 SDK 新名 | 出现位置 |
|---|---|---|
| `DDL_PMU_DisableIT_PVD()` | `DDL_PMU_DisableIT_PVD_IE()` | drv_system.c:139, 188 |
| `DDL_PMU_DisablePVDLT()` | `DDL_PMU_DisablePVDBelowThresholdMonitoring()` | drv_system.c:140 |
| `DDL_PMU_DisablePVDHT()` | `DDL_PMU_DisablePVDAboveThresholdMonitoring()` | drv_system.c:141 |
| `DDL_PMU_IsEnabledIT_PVD()` | `DDL_PMU_IsEnabledIT_PVD_IE()` | drv_system.c:151 |

（`DDL_PMU_EnablePVDLT/HT` 我们本来就不用；`DDL_PMU_IsEnabledPVD`、`SetPVDVoltageThreshold`、`SetPVDFilterLength`、`EnableFilter`、`ClearFlag_PVDF`、`DDL_RCC_Disable_PVDRST`、`DDL_RCC_IsEnabled_PVDRST` 两版同名，可直接用。）

### 0.3 工具链：**不需要迁移到 AC6**

- 目标：`pArmCC 5060750::V5.06 update 6::ARMCC`，`uAC6=0`，`uC99=1`，`uGnu=1`，Optim 4，无 ScatterFile。
- 已扫描 aurora `driver/` + `app/` 全部源码：**无任何 C11 专属构造**（无 `_Static_assert`/`_Generic`/`_Alignas`/`_Noreturn`/`<stdalign.h>`…）→ AC5 `--c99` 足够。
- 唯一需要条件化的是 `drv_system.h` 的 `DRV_SYSTEM_NORETURN`（`__attribute__((noreturn))`），AC5 需改用 `__declspec(noreturn)` 或直接置空。
- 建议在目标 uvprojx 的 C `MiscControls` 补 `--diag_error=warning` 以近似 aurora 的 `-Werror`（**列入 G1 任务，非可选**）。

### 0.4 目标工程内存映射已核实一致（无冲突）

- 生效 IROM1 = **0x1000 / 0xEE00**（0x1000~0xFDFF），IRAM1 = 0x20000000 / 0x2000（8 KiB）。
- `Application/app/src/system_g32f031.c` 已有 `VECT_TAB_OFFSET 0x1000` + `SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET` ✅。
- uvprojx 里那份 `0x0 / 0x10000` 是器件描述默认块，非链接生效块，不是冲突。
- ⚠ **待 G0 冻结的冲突**：aurora `BOARD_FLASH_PAGE_B = 0x0000FE00` 与目标 **OTA 标志页 0xFE00~0xFFFF（magic `0x4F544131`）完全重叠**；`PAGE_A = 0xFC00` 落在应用代码区内需显式预留。G7 才用到，但**必须在 G0 一次性冻结**（建议 PAGE_A=0xF800 / PAGE_B=0xFA00，同时 IROM1 收窄为 0x1000/0xE800）。

### 0.5 IWDT 需要先开 APB 时钟（aurora 漏了）

官方 `IWDT_Reset` 例程：`DDL_RCC_Unlock(); DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_IWDT); DDL_RCC_Lock();` 之后才配置。
aurora `drv_watchdog.c` **没有这一步** → G2-D 移植时必须补上。

### 0.6 PVD 中断路径 = EINT Line 16 + `PVD_IRQn`

官方 `PMU_PVD` 例程通过 `DDL_EINT_LINE_16` + `NVIC_EnableIRQ(PVD_IRQn)` 产生 PVD 中断。
我们的设计**明令禁止 PVD 中断与 PVD 复位** → 目标工程**不得**初始化 EINT16、**不得**实现 `PVD_IRQHandler`（保留 startup 的 weak 默认），并在 `drv_system.c` 保留 `#error` 编译期护栏。

### 0.7 PVD 门限有 100 mV 迟滞与 ±2.5% 器件容差（双方原方案均漏）

证据：用户手册 `PMU_PVDCSR.PVDTHSEL[2:0]` 档位表 + 数据手册表格 34「可编程电压检测器特性」

| PVDTHSEL | 上升沿阈值 | 下降沿阈值 | aurora 枚举 |
|---|---|---|---|
| 000 | 2.0 V | 1.9 V | `THRESHOLD_1` |
| 001 | 2.4 V | 2.3 V | `THRESHOLD_2` |
| **010** | **2.8 V** | **2.7 V** | **`THRESHOLD_3`** ← 当前候选 |
| 011 | 3.2 V | 3.1 V | `THRESHOLD_4` |
| 100 | 3.6 V | 3.5 V | `THRESHOLD_5` |
| 101 | 4.0 V | 3.9 V | `THRESHOLD_6` |
| 110 | 4.4 V | 4.3 V | `THRESHOLD_7` |
| 111 | 4.8 V | 4.7 V | `THRESHOLD_8` |

- `VPVDhyst` 典型 **100 mV** → **「稳定」判定点是 2.8 V，「掉落」判定点是 2.7 V**，不是同一个电压。
- 表格 34 给出器件容差（以 000 档为例：上升沿 min/typ/max = 1.96/2.00/2.05 V，约 ±2.5%）→ 2.8 V 档实际可能落在约 2.74~2.87 V。
- **推论（写进 G2-A 台架用例）**：必须分别实测**上升沿触发点与下降沿释放点**两个电压，只测一个点得不出可用结论；`BOARD_MCU_SUPPLY_STABLE_TIME_MS` 的窗口重启行为天然带 100 mV 回差，验收记录必须写明这一点。
- aurora 的枚举推导链（`BOARD_MCU_PVD_THRESHOLD_MV == 2800UL → THRESHOLD_3`；`BOARD_MCU_PVD_FILTER_US == 50UL → FILTER_LENGTH_3200`，50 µs @64 MHz = 3200 cycles）经核实**数值正确**，必须保留 `#if/#error` 推导，**禁止**在移植时直接硬写枚举（否则 mV 宏与枚举形成双真源）。

### 0.8 复位原因寄存器 = `RCC->RSTCSR`（8 个标志，非 4 个）

证据：`Libraries/Device/Geehy/G32F031/Include/g32f031xx.h`

| bit | 标志 | 含义 | Bring-up 处置 |
|---|---|---|---|
| 0 | `RCC_RSTCSR_OPTRSTFLG` | 选项字节重载复位 | 记录 |
| 1 | `RCC_RSTCSR_NRSTRSTFLG` | NRST 引脚复位 | 记录（非 "PINRST"） |
| 2 | `RCC_RSTCSR_PVDRSTFLG` | PVD 系统复位 | **⚠ 报警项**：我们编译期禁 PVD 复位，读到 1 说明 `PVDRSTEN` 被别处打开，属严重违规，日志须显式告警 |
| 3 | `RCC_RSTCSR_SWRSTFLG` | 软件复位 | 记录 |
| 4 | `RCC_RSTCSR_IWDTRSTFLG` | IWDT 复位 | G2-D 判据依据 |
| 5 | `RCC_RSTCSR_WWDTRSTFLG` | WWDT 复位 | 我们不用 WWDT，读到 1 亦为异常 |
| 6 | `RCC_RSTCSR_LOCKUPRSTFLG` | Cortex-M0+ lockup 复位 | **与 G1 HardFault 策略交互**：须在 G1 决定 `LOCKUPRSTEN(bit15)` 开或关，并与 `HardFault_Handler` 是否主动 SWRST 保持一致 |
| 7 | `RCC_RSTCSR_PORRSTFLG` | POR/PDR 复位 | 弱光反复上电的主要来源，须计数 |

使能位：`PVDRSTEN(bit14)`、`LOCKUPRSTEN(bit15)`。启动日志 `RESET=` 字段须**读取后清除**，避免下次复位原因混叠。

---

## 1. 总体红线（每批都要复查，写进代码注释）

1. **拓扑红线**：单路**异步 Boost**。PA15/GLC = ATMR_CH0 唯一主 PWM；PA14/GHC 有网络但**无上管 MOS** → 永久 GPIO 低。禁止移植 120W 时代的互补 PWM / 死区 / DCM-CCM 同步整流。
2. **首条指令链红线**：GLC=LOW、GHC=LOW、Relay=OFF、LINK=OFF、PWM 主门/输出许可=OFF，必须在任何外设业务初始化之前完成。
3. **弱光红线**：VDD 掉落只清零稳定计时器、重启完整窗口，**永不生成软件 Fault**、永不提前启动 IWDT/PWM/ADC。
4. **门禁红线**：`DRV_DEVICE_GATE_*` / `DRV_DEVICE_POWER_OUTPUT_ALLOWED` 是**最终验收证据门**，不是台架使能开关，**严禁**为了让某阶段跑起来而预置为 1。
5. **看门狗红线**：IWDT 是软件卡死兜底，**不是 MOS 微秒级保护**。
6. **Gate 判定红线**：代码完成 + Keil 编译 0 error/0 warning + 静态验证通过 + 实板验证通过 + 测试记录归档 = 才允许进下一门；否则只能是 IN_PROGRESS 或 BLOCKED，**永不写 PASS**。
7. **两层架构红线**：只有 `driver/src/drv_*.c` 允许碰寄存器/DDL；`driver/` 不得 include 任何 `app/` 头文件，`app/` 不得直接调 `DDL_*`。
8. **`MIGRATION_GATE` 红线**：当前 Gate 之外的硬件一律不编译、不调用；该宏只能由人工在推进门禁时 +1，**不得**为了让某个台架用例跑通而临时上调。

### 1.1 `MIGRATION_GATE` 编译期门禁机制（采纳外部方案，落点为 `drv_device.h`）

放在 `Application/driver/inc/drv_device.h`（跨模块开关由驱动层自持；**不放** `app/` 下任何头文件，否则 driver 反向依赖 app）：

```c
/* 当前允许编译/运行到的 Bring-up 门禁；仅人工推进，禁止为跑通台架临时上调。 */
#define DRV_DEVICE_MIGRATION_GATE            (1U)

#if (DRV_DEVICE_MIGRATION_GATE > 5U)
#error "DRV_DEVICE_MIGRATION_GATE > 5 is out of this migration plan's scope"
#endif
```

用法约定：

- `drv_device_config()` / `main()` / `drv_*_init()` 的调用链按 `DRV_DEVICE_MIGRATION_GATE` 裁剪，**未到该 Gate 的外设一律不 Init**。
- G2-C 启动日志的 `MIGRATION_GATE=` 字段直接由该宏渲染，**日志值与编译值同源**，不允许手写字符串。
- 每个 Gate 的静态验收项：用 MAP / 反汇编确认「当前 Gate 不该出现的东西确实不在」（见各 Gate 的静态验证条目）。
- 该宏**不替代**、**不影响** `DRV_DEVICE_GATE_*` / `DRV_DEVICE_POWER_OUTPUT_ALLOWED`：前者是"编译到哪一步"，后者是"最终验收证据门"，两套语义严格分离。

---

## 2. 批次 1 = G0 + G1（PinMap 冻结 + 最小安全工程）

### G0 冻结（纯文档，无代码）

- [ ] G0-1 在 aurora 仓建 `docs/bringup/G00-PinMap与BOM冻结/README.md`，抄录 §0.1 的数据手册 AF 表证据（页码 29~30，表格 9）
- [ ] G0-2 逐网络人工比对原理图 `14-REF-硬件原理图-V0.1.pdf`（两工程 md5 一致，同一张图），确认 6 路 ADC + COMP + Relay + LINK + 两路串口，签字归档 `RESULT.md`
- [ ] G0-3 冻结 Flash 映射：Bootloader 0x0000~0x0FFF / Application 0x1000~? / 参数页 A、B / OTA 标志页 0xFE00~0xFFFF，解决 §0.4 的 PAGE_B 重叠，写进 `docs/bringup/G00-*/flash-map.md`
- [x] G0-4 冻结中断优先级表（COMP0/COMP1_2_3/ATMR_BRK=0，DMA_CH1=1，ADC/SysTick=2，USART/UART=3；已归档 G00 README §6）
- [ ] G0-5 明确标注**已知量程风险**：BST_U 分压 125k/5k≈26:1 → 理论满量程 ≈85.8 V，**87~93 V 不可当作可准确测量区间，软件系数无法恢复饱和信息**
- [ ] G0-6 冻结 `LOCKUPRSTEN` 取舍（§0.8 bit6/bit15），并与 G1 `HardFault_Handler` 是否主动 SWRST 的决策写在同一处

**G0-2 的冲突基线表必须逐字准确（外部方案此处转述有误，不得沿用）：**

| 网络 | 目标工程原 `app_hw_config.h` 原文（该文件已于代码风格重构时删除） | aurora `board_config.h` / docs 46 | 数据手册可行性 | G0 裁决 |
|---|---|---|---|---|
| PA8 | 逻辑索引 0 = PV_U | PV_I（`ADC_CH_PV_I=1`） | PA8 = ADC_IN1 | 取 aurora |
| PA9 | 逻辑索引 1 = BAT_U | PV_U（`ADC_CH_PV_U=2`） | PA9 = ADC_IN2 | 取 aurora |
| PB0 | 逻辑索引 2 = BST_U | BAT_U（`ADC_CH_BAT_U=3`） | PB0 = ADC_IN3 | 取 aurora |
| PB1 | **未出现** | BST_U（`ADC_CH_BUS_U=4`） | PB1 = ADC_IN4 / COMP1_INN / VREF+ | 取 aurora |
| PB12 | 逻辑索引 3 = NTC_MOS | NTC_MOS（CH5） | PB12 = ADC_IN5 | 一致 |
| PB5 | 逻辑索引 4 = NTC_AMB | NTC_AMB（CH6） | PB5 = ADC_IN6 | 一致 |
| COMP0 | PA7(IN+)、PGA0/OPA0(IN−)、PB10(OUT) | 同 | PB10 AF7=COMP0_OUT | 一致 |
| COMP2 | **原文未写** | PB6(IN+) / PB2(IN−) | PB6=COMP123_INP，**PB2**=COMP2_INN | 取 aurora |

> 注：外部方案称「现 Application：PA8=PV_U、PB0=BST_U、**PB1=COMP2−**」——前两项对，**PB1=COMP2− 是错的**：`app_hw_config.h` 原文没有这一句，且数据手册中 COMP2_INN 在 **PB2**。G0 的全部价值就是逐字准确，不得引用转述版本。

### G1 最小安全工程（代码，`/code_wrt`）

目标工程新增/修改文件：

| 动作 | 文件 | 说明 |
|---|---|---|
| 新增 | `Application/driver/inc/drv_device.h` / `src/drv_device.c` | 迁移门禁 + 后期功能预留开关（OTA/蓝牙）+ 最终验收证据门 + 板级初始化入口 `drv_device_config()`（按 Gate 裁剪）；不含任何 DDL/app 头 |
| 新增 | `Application/driver/inc/drv_io.h` / `src/drv_io.c` | 从 aurora 搬入，数字控制引脚表 + 板级极性 + 功率 GPIO 安全态 + LockKey |
| 新增 | `Application/driver/inc/drv_system.h` / `src/drv_system.c` | 弱光供电资格配置内置；本批只启用时钟 + SysTick 1ms + IRQ 优先级，PVD 部分留在 G2 打开 |
| 新增 | 配置骨架头 `drv_adc.h` `drv_board.h` `drv_pwm.h` `drv_comp.h` `drv_flash.h` `drv_debug_uart.h` `drv_watchdog.h` | **只含配置宏、不含函数声明**，承接原 `board_config.h` 的冻结硬件参数（Flash 页地址已按 §0.4 下移一页并加重叠 `#error`；~~`DRV_PWM_DEADTIME_TICKS` 加 `#error` 锁死为 0~~ → 该宏与 `#error` 已按 Jovi 裁决删除，见下方"裁决落地"行） |
| 改写 | `Application/app/inc/main.h` | 只放 app 层跨文件共用参数（当前 `APP_RUN_LED_HALF_PERIOD_MS`），**不再聚合任何驱动头** |
| 改写 | `Application/app/src/main.c` | **删除** `DRV_PWM_SetDuty(500)` / `SetComplementary(1)` / `DRV_PWM_Start()` / `DRV_ADC_Start()` / `BSP_OTA_Process()`；改为 `drv_device_config()` + RUN 灯 1Hz 主循环；按需 include `main.h`/`drv_device.h`/`drv_io.h`/`drv_system.h` |
| 修改 | `Application/app/src/g32f031_int.c` | `SysTick_Handler` → `drv_time_tick_isr()`；`HardFault_Handler` → 先 `drv_io_force_power_safe()` 再停机（与 §0.8 `LOCKUPRSTEN` 决策一致） |
| 合并 | legacy `bsp_{adc,pwm,analog}.*` → `drv_{adc,pwm,comp}.*` | 同一外设不得同时存在 `bsp_xxx.h` 与骨架 `drv_xxx.h`，故合并而非等重写。OPA 随 COMP 合入 `drv_comp.*`（与 aurora 一致）。函数名 `BSP_*` → **`DRV_*` 全大写**（Jovi 明确要求，大写是"实现体仍是 legacy"的可视标记，重写时才降小写）。旧配置宏落在同文件的 `LEGACY` 分节（`DRV_ADC_LEGACY_CHANNEL_COUNT=5` 对冻结的 6，**故意不统一**，改值属行为变更须随 G3 重写一并做；`DRV_PWM_LEGACY_DEADTIME_TICKS=32` 见下方裁决行） |
| 裁决落地 | `drv_pwm.h` / `app/{inc,src}/g32f031_int.{h,c}` | Jovi 2026-09-01 三项裁决：① **死区语义纠正**——死区是互补输出专有概念，冻结基线单端输出**不设死区项**（而非"设成 0"），legacy 互补的 `32U` 是正当值；已删除 `DRV_PWM_DEADTIME_TICKS (0U)` 及其 `#error`（删前确认零引用），红线实际执行点是 `drv_io.h` 的 `DRV_IO_PIN_GHC_UNUSED_*`。② **M0+ 三个不存在的异常 handler 保留**（"免得以后使用"）——声明保留 + **补回三个空定义**，已验证 startup 向量表无对应项/无弱符号、外部链接不触发 `#177-D`。③ `drv_device_reset()` 零调用点保留正确，无动作。详见 `docs/46-...` §23 与 `docs/bringup/G01-最小安全工程/RESULT.md` §10 |
| 修改 | legacy `driver/{inc,src}/bsp_ota.{h,c}` | 聚合头删除后补齐真正需要的 DDL include；`BSP_OTA_*` 函数名与文件名**保持 `bsp_` 不动**（Jovi：除 OTA 外的驱动文件都改 `drv_`）；内部改调 `DRV_PWM_Stop()` |
| 删除 | `driver/inc/board_config.h`、`driver/inc/driver.h`、`app/inc/app_hw_config.h`、`driver/{inc,src}/g32f031_device_cfg.{h,c}` | 代码风格重构：取消无层前缀的配置文件、取消只做转发的聚合头、取消 app 侧硬件配置文件；配置改由各驱动 `.h` 自持 |
| 修改 | `Application/Project/MDK/IAP_Application.uvprojx` | 新增 group `driver`；IncludePath 追加 `..\..\driver\inc`；C MiscControls 补 `--diag_error=warning`；`g32f031_device_cfg.c` 条目改为 `drv_device.c`；`bsp_{adc,pwm,analog}.c` 三条改为 `drv_{adc,pwm,comp}.c`（`.uvoptx` 同步） |
| 清理 | `Application/Project/MDK/Objects/` | 删除 `bsp_pvd.o` / `bsp_debug.o` 等陈旧产物（对应源文件已不存在，留着会误导） |

- [x] G1-1 板级配置搬入 + Flash 页地址修正 + PB7/PB8 注释修正 + `DRV_DEVICE_MIGRATION_GATE` 引入（落点经风格重构后为 `drv_device.h` + 各驱动 `.h`）
- [x] G1-2 `drv_io.c/h` 搬入（GLC/GHC/Relay/LINK/LED/DEBUG_TX 安全态 + `DDL_GPIO_LockKey`）
- [x] G1-3 `drv_system.c/h` 搬入（本批只留时钟 + 1ms 节拍 + IRQ 优先级；`DRV_SYSTEM_NORETURN` 适配 AC5）
- [x] G1-4 改写 `main.c` 为最小安全启动链
- [x] G1-5 修 `g32f031_int.c`（SysTick 接节拍；HardFault 强制安全态）
- [x] G1-6 清理自动上电启动（原 `g32f031_device_cfg.c`，现已被 `drv_device.c` 取代）+ `Objects/` 陈旧 `.o`
- [x] G1-7 更新 uvprojx（group / IncludePath / MiscControls / `drv_device.c` 条目）
- [x] G1-8 Keil 编译 0 error / 0 warning，检查 MAP 未越界 0x1000~IROM 上限、未压到参数页/OTA 页
- [x] G1-9 **静态验证**：MAP / 反汇编确认镜像内不存在 `DRV_PWM_Start` / `DRV_ADC_Start` / `BSP_OTA_Process` 的调用，ATMR MOE 与 ADC DMA 均未被使能
- [ ] G1-10 实板：≥100 次混合上电/复位循环 —— GLC 无毛刺、GHC 恒低、Relay 无误吸合、HardFault 注入后功率 GPIO 仍安全（示波器 + 记录）**← 当前唯一未闭合腿，G1 因此仍为 IN_PROGRESS**
- [x] G1-11 可选：PB9/LED_RUN 慢闪，作为"主循环还活着"的肉眼指示
- [x] G1-12 归档 `docs/bringup/G01-最小安全工程/{README.md,RESULT.md}`（示波器截图待 G1-10 补入）

---

## 3. 批次 2 = G2（弱光供电资格 + 时钟 + 1ms 节拍 + Debug UART + 看门狗）

**严格按此顺序落地**（看门狗最后，才能用 UART+节拍区分「真 WDG 复位」与「时钟错/死锁/供电不稳」）。

### G2-A 弱光供电资格（PVD 仅作启动资格）

- [ ] G2-A1 打开 `drv_system.c` 的 PVD 段，套用 §0.2 的 4 处 API 改名；**保留 `#if/#error` 枚举推导链，禁止硬写 `THRESHOLD_3`/`FILTER_LENGTH_3200`**（避免 mV 宏与枚举双真源）
- [ ] G2-A2 保留 `#error` 护栏：`BOARD_MCU_PVD_RESET_ENABLE` / `BOARD_MCU_PVD_IRQ_ENABLE` 非 0 即编译失败；**不初始化 EINT16、不实现 `PVD_IRQHandler`**
- [ ] G2-A3 启动链严格为：最小安全 GPIO → PVD Ready（超时 `BOARD_MCU_PVD_READY_TIMEOUT_US`，`__WFI()` 等待；**超时只表示 PVD 模块异常，不等于弱光**）→ VDD 连续稳定 → 满 `BOARD_MCU_SUPPLY_STABLE_TIME_MS` 窗口 → 才初始化全部外设；任一次掉落 → 清零计时器、**重启完整窗口**、不产 Fault
- [ ] G2-A4 资格通过后**关闭 PVD**（运行期不把 PVD 当弱光 Fault 源）
- [ ] G2-A5 台架 7 组输入：快速上升 / 慢坡 / 阈值附近三角波 / 阈值附近周期抖动 / 50 ms 后掉落 / 99 ms 后掉落 / 101 ms 后保持
- [ ] G2-A6 **按 §0.7 分别实测上升沿触发点与下降沿释放点两个电压**（100 mV 迟滞 + ±2.5% 器件容差），只测一个点不算数
- [ ] G2-A7 判定：未满窗口**绝不**开 UART/IWDT/ADC/PWM；满窗口只完整启动一次
- [ ] G2-A8 用实测冻结阈值与窗口（2.8 V / 100 ms 目前只是候选值），回填 `drv_system.h` 的 `DRV_SYSTEM_PVD_*` / `DRV_SYSTEM_SUPPLY_*` 并归档

### G2-B 时钟与 1ms 节拍

- [ ] G2-B1 确认 HSI 64 MHz、`DDL_FLASH_LATENCY3`、AHB/APB DIV_1、`SystemCoreClockUpdate()` 后 `SystemCoreClock == 64000000`
- [ ] G2-B2 `SysTick_Config(SystemCoreClock/1000)` 并打开 TICKINT；`SysTick_Handler` → `drv_time_tick_isr()`；`drv_time_now_ms()` 供 PVD 与喂狗窗口共用
- [ ] G2-B3 **1 ms 任务入口翻转 PB11/LED_FAULT** 供示波器测量，仅在 `DRV_DEVICE_MIGRATION_GATE == 2U` 下编译。**严禁**使用 PA15/GLC、PA14/GHC、PA13/RELAY、**PA12/LINK** —— 后两者是带负载的功能输出，1 kHz 翻转会驱动真实网络
- [ ] G2-B4 示波器记录周期均值/最小/最大、峰峰抖动、丢拍数；判据：均值误差在 HSI 手册容差内、无丢拍/重拍、空载峰峰抖动 < 10 µs（**不得为了"通过"去改日志或放宽门槛**）
- [ ] G2-B5 归档 `docs/bringup/G02-B-1ms节拍/{scope-tick.png,meter-data.csv}`

### G2-C Debug UART（PB7/PB8，独立 UART 外设；蓝牙/产品协议后置）

- [ ] G2-C1 新增 `Application/driver/inc/drv_debug_uart.h` / `src/drv_debug_uart.c`：以 aurora `drv_uart.c` 为模板，改为 **`UART` 外设 + PB7/PB8 + `DDL_GPIO_AF_0` + `DDL_APB_GRP1_PERIPH_UART` + `NVIC_EnableIRQ(UART_IRQn)`**，沿用「一帧整体入队、不留半帧」的环形缓冲策略
- [ ] G2-C2 `g32f031_int.c` 新增 `UART_IRQHandler`（startup 已有 weak 符号）；**ISR 内禁止 printf、禁止格式化**
- [ ] G2-C3 TX 空间不足时**丢弃整行并递增溢出计数**（新增 `debug_tx_drop_count`），绝不阻塞 1 ms 任务、绝不留半帧
- [ ] G2-C4 启动日志字段：`BOOT / FW_VERSION / GIT_SHA / RESET_CAUSE / CLOCK_HZ / PVD_STABLE_MS / MIGRATION_GATE`
  - `RESET_CAUSE` 按 §0.8 解析 `RCC->RSTCSR` 全部 8 个标志，**读取后清除**；`PVDRSTFLG` / `WWDTRSTFLG` 置位时输出显式告警
  - `MIGRATION_GATE` 由 `DRV_DEVICE_MIGRATION_GATE` 宏渲染，与编译值同源，不得手写字符串
- [ ] G2-C5 测试：115200 8N1；固定图样连续发；RX 回环；随机长度帧；缓冲溢出；连续 10^6 字节
- [ ] G2-C6 判据：10^6 字节回环零错；RX 满有统计计数且无越界写；日志发送不阻塞 1 ms 任务（用 G2-B3 的翻转脚同屏对照）
- [ ] G2-C7 **产品/蓝牙协议不在本批范围**：PA10/PA11 的 `USART` 保持未初始化；`BSP_OTA_Init/Process` 由 `DRV_DEVICE_MIGRATION_GATE` 关闭（台架期间不留能跳 Bootloader 的未验证路径）
- [ ] G2-C8 归档 `docs/bringup/G02-C-DebugUART/{README.md,loopback-1e6.log,RESULT.md}`

### G2-D 看门狗（最后）

- [ ] G2-D1 新增 `Application/driver/inc/drv_watchdog.h` / `src/drv_watchdog.c`（aurora 版）+ **补 `DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_IWDT)`**（§0.5）。IWDT DDL 为头文件内联，**不需**新增 `.c` 到 uvprojx
- [ ] G2-D2 数值对齐 aurora `app_config.h`：`TIMEOUT_MS = 1000`、`STARTUP_GRACE_MS = 500`、`WINDOW_MS = 100`
- [ ] G2-D3 本阶段只监督两张健康票：主循环票 + 1 ms 控制票（ADC 票留到 G4）
- [ ] G2-D4 **不变量：1 ms ISR 只置票据，绝不调用 `drv_watchdog_feed()`**；只有主循环的 service 函数在两张票都到齐时才 feed。（若 ISR 直接喂狗，主循环卡死将无法被捕获，票据机制失效）
- [ ] G2-D5 故障注入：主循环 `while(1)` 卡死 / 停掉 1 ms 任务
- [ ] G2-D6 判据：故意停喂必在预期窗口复位；WDG 复位不产生 GLC/Relay 毛刺；2 h 正常运行无误复位；复位原因正确显示为 `IWDTRSTFLG`
- [ ] G2-D7 归档 `docs/bringup/G02-D-看门狗/{README.md,reset-window.csv,RESULT.md}`

### G2 收口

- [ ] G2-E1 Keil 编译 0 error / 0 warning
- [ ] G2-E2 uvprojx 追加 `drv_debug_uart.c` / `drv_watchdog.c`
- [ ] G2-E3 **静态验证**：`DRV_DEVICE_MIGRATION_GATE == 2U` 时，用 MAP / 反汇编确认镜像内 **ATMR MOE 未被使能、ADC DMA 未被开启、`DRV_OPA/COMP/PWM/ADC_Init` 与 `BSP_OTA_*` 均无调用**
- [ ] G2-E4 `docs/bringup/G02-*/RESULT.md` 汇总，未全绿则标 IN_PROGRESS

---

## 4. 批次 3 = G3 + G4 + G5（ADC 采样链 + DMA 双缓冲 + 工程量标定）

> **前提**：本批代码已把 `DRV_DEVICE_MIGRATION_GATE` 一次推到 `5U`（G3 软触发 / G4 DMA 以 `#if GATE==3` 与 `#if GATE>=4` 互斥保留）。实板仍须按 3→4→5 取证，未 PASS 不得把本批标绿。
>
> 2026-09-02：Keil Rebuild **0 Error / 0 Warning**（`rebuild_g5.log`，Code=10192）。listing 含 `g32f031_ddl_dma.o`，无 `DRV_ADC_Init` / `DRV_ADC_IRQHandler`，无 `EnableAllOutputs`。实板未做。

> **⚠ 不得冒充**：旧五通道 EOS / `DRV_ADC_*` 已删除。G4 取证必须针对新 DMA 双半缓冲路径。

### G3 单通道原始码验证（**不上 DMA**）

> **Jovi 2026-09-02**：G2 不配 PB1、不 Init ADC。PB1 不是漏移植。G3 新写路径一次性落地四件，禁止给 legacy 5 通道 `drv_adc.c` 补 PIN_1 冒充六通道。详见 doc46 §5.0 / §23.8。

- [x] G3-0a GPIO 模拟掩码含 `PIN_1`（PB1 / ADC_IN4 / BST_U）
- [x] G3-0b rank / 通道表含 CH4
- [x] G3-0c Sequencer 与缓冲 / 索引从 5 改为 6（`CHANNEL_COUNT`、缓冲数组、采样时间表一并改）
- [x] G3-0d 对外命名统一 **BST_U**；`BUS_U` 只允许一次性废弃别名，不得当成第二路
- [x] G3-0e **G3 PASS 前置（书面）**：BST_U 26:1 → 理论满量程 ≈85.8 V；近满量程码 4080 标不可信。**注入/超参考台架未做**
- [x] G3-1 新写 `drv_adc.h/.c`：`GATE==3` 软触发单通道；`GATE>=4` DMA。legacy `DRV_ADC_*` 已删
- [ ] G3-2 逐通道注入 0.0 / 0.5 / 1.0 / 1.5 / 2.0 / 2.5 / 3.0 V，打印 `channel / raw / mean_100 / min / max / peak_to_peak / vdd_mv`
- [ ] G3-3 对照 `Code_ideal = Vin / Vref * 4095`；**一次只动一条网络**确认物理映射
- [ ] G3-4 BAT_U 高源阻抗建立时间专项：前一通道分别置近 0 V 与近 3.3 V，比较 BAT_U 读数差
- [ ] G3-5 判据：6 通道映射 100% 一致、原始码单调、0~3.0 V 无跳码、单通道误差 ≤0.5%FS、BAT_U 建立时间合格、VREF/VDD 实测记录
- [x] G3-6 归档 `docs/bringup/G03-ADC原始码/RESULT.md`（**IN_PROGRESS**，无 meter-data）

### G4 定时触发 + 6 通道扫描 + DMA 双半缓冲

- [x] G4-1 `g32f031_ddl_dma.c` 加入 uvprojx（Rebuild 已编入 `g32f031_ddl_dma.o`）
- [x] G4-2 GTMR 预分频 63 / 自动重载 99 → 10 kHz TRGO；ADC 规则组 6 rank（CH1~CH6），DMA CH1 循环双块，16 次扫描/块
- [x] G4-3 DMA 索引顺序固定 `0=PV_I,1=PV_U,2=BAT_U,3=BST_U,4=NTC_MOS,5=NTC_AMB`
- [x] G4-4 ISR **只允许**：识别完成块、记录 block/sequence、置 ADC 票、清中断。**禁止**温度查表 / MPPT / printf / Flash / feed
- [x] G4-5 埋点 `producer_block / producer_sequence / consumer_sequence / DMA overwrite_count / ADC overrun_count`
- [ ] G4-6 主循环读稳定快照；**实板**验证 stale 与 overrun 可检出
- [x] G4-7 SRAM 预算复核（8 KiB 总量：`g_adc_dma[2][96]` = 384 B；本镜像 ZI=1968）
- [x] G4-8 看门狗新增 ADC 健康票（ISR 只置不喂）。**2 h 无误复位未做**
- [x] G4-9 **静态验证（listing）**：`g32f031_ddl_dma.o` 已链接、无 `DRV_ADC_*` 旧符号、无 `EnableAllOutputs`。完整 MAP 文件未开
- [x] G4-10 归档 `docs/bringup/G04-ADC-DMA/RESULT.md`（**IN_PROGRESS**）

### G5 工程量换算与标定（上电前最关键一门）

- [x] G5-1 `drv_board.h/.c`：`Physical = K*(code-zero)*polarity/den`（BAT_U 走 int64）
- [x] G5-2 电压链理论值写入头文件：PV_U ≈20.95 mV/code；BAT_U ≈24.51 mV/code；BST_U 满量程 ≈85.8 V
- [x] G5-3 BST_U 量程风险落档；`DRV_ADC_NEAR_FULL_SCALE_CODE(4080)` 日志打 `!SAT`。**87~93 V 台架未做**
- [x] G5-4 PV_I 理论链写入（OPA×16 / zero=2048 / +1）。**G7 前 OPA 未 Init，不能当产品电流**
- [ ] G5-5 标定点：PV_U 0/5/10/20/30/40/50 V；BAT_U 0/10/20/40/48/60/72/84 V；BST_U 0/10/20/40/60/75/82 V；PV_I 本阶段 0/0.5/1/2 A
- [ ] G5-6 NTC：短路/开路/0/25/60/95/105 °C；25 °C 理论 raw ≈3896
- [ ] G5-7 初始精度门：电压 ≤1% 读数；PV_I(≥2 A) ≤3% 读数；NTC ≤2 °C
- [x] G5-8 理论系数只在 `drv_board.h/.c`；禁止写入 `main.h`
- [x] G5-9 `DRV_DEVICE_GATE_ANALOG_CALIBRATED=0`；`DRV_DEVICE_POWER_OUTPUT_ALLOWED=0`
- [x] G5-10 归档 `docs/bringup/G05-工程量标定/RESULT.md`（**IN_PROGRESS**）

---

## 5. 风险与阻塞项

| 级别 | 项 | 处置 |
|---|---|---|
| ⚠⚠ | `ATMR_CR1_UDISEN` 两库位定义不同（bit1 vs bit2）+ `DDL_ATMR_InitTypeDef` 字段顺序不同 | 本次 G0~G5 不涉及 ATMR 业务；**G6 必须对着目标头文件重写 `drv_pwm.c`，禁止逐行照抄 aurora** |
| ⚠⚠ | aurora `BOARD_FLASH_PAGE_B 0xFE00` 与目标 OTA 标志页重叠 | G0-3 一次性冻结新地址并同步收窄 IROM1 |
| ⚠ | 目标 uvprojx 曾缺 `g32f031_ddl_dma.c` | G4-1 已加入；`rebuild_g5.log` 已链接 `g32f031_ddl_dma.o` |
| ⚠ | aurora `drv_watchdog_init()` 缺 IWDT APB 时钟使能 | G2-D1 补齐 |
| ⚠ | 目标 `GPIO_Config()`/`RCM_PeripheralClkConfig()` 为空、`SysTick_Handler` 为空、`HardFault_Handler` 裸 `while(1)`、启动即 50% 占空比 + 互补输出 | G1 全部改掉；这是当前工程**连 G1 都不满足**的根因 |
| ⚠ | 目标文档提到的 `bsp_pvd.c` / `bsp_debug.c` / `APP_PWM_COMPLEMENTARY` 实际不存在（只剩 stale `.o`） | 不得假定存在；G1 清理 `Objects/` 陈旧产物 |
| ⚠ | aurora 文档提到的 `BOARD_USART_MODE` / `BOARD_USART_MODE_DEBUG` 代码中不存在 | 目标工程改用两路独立外设（USART=PA10/11 产品，UART=PB7/8 调试），不再需要模式宏 |
| 信息 | AC5 已停止维护 | 本次不迁移；aurora 代码已确认 C99 兼容，保留目标验证过的 AC5 基线 |
| ⚠ | PVD 门限 100 mV 迟滞 + ±2.5% 容差（§0.7），2.8 V 只是候选值 | G2-A6 必须实测上升/下降两个点后再冻结 |
| ⚠ | `RCC->RSTCSR` 有 8 个标志且 `PVDRSTFLG`/`LOCKUPRSTFLG` 语义敏感（§0.8） | G2-C4 全解析 + 读后清除；`LOCKUPRSTEN` 决策在 G0-6 冻结 |
| ⚠ | 旧 `DRV_ADC` 五通道 EOS 形似 G4 | 已删除；G4 取证只针对 DMA 双半缓冲路径 |
| ⚠ | `BSP_OTA_Process()` 在台架期留着 = 存在未验证的跳 Bootloader 路径 | G1 从 main 删除、G2 由 `MIGRATION_GATE` 关闭 |
| ⚠ | 1 ms 节拍探针若选 PA12/LINK、PA13/RELAY、PA14/GHC、PA15/GLC，会驱动真实功率/通信网络 | 只允许 PB11/LED_FAULT，且仅 `MIGRATION_GATE == 2U` 编译（G2-B3） |
| ⚠ | 若 1 ms ISR 直接喂狗，主循环卡死无法被捕获 | G2-D4 不变量：ISR 只置票，主循环 service 才 feed |

---

## 6. 实现约束（`/code_wrt` 与验证口径）

### 6.1 编码规范（沿用 `/code_wrt`）

- 两阶段：**ponytail 写最小实现 → code_zl 补注释**，不在同一遍里边写边注释
- `.c` 文件分节用 `/* 标题 */`；`.h` 文件分节用 `//====` 风格
- 文件级 `static` 变量/函数声明**全部集中在第一个公开函数之前**
- `*_init()` 只写分节调用，**不在 init 里堆逐行 DDL 寄存器操作**；寄存器细节下沉到本文件的 static 子函数
- 只有 `driver/src/*.c` 可以出现 DDL / 寄存器；`app/` 出现 `DDL_` 前缀即视为违规（红线 #4）

### 6.2 静态验证口径（重要）

**目标工程没有 Host CI，也没有单元测试框架**（aurora 的 `test/` 不随本次移植过去）。因此每个 Gate 的「静态腿」判据统一定义为：

1. Keil **Rebuild All** → 0 error / 0 warning（`MiscControls` 带 `--diag_error=warning`）
2. 检查 `.map`：确认**应当存在**的符号在、**应当不存在**的符号不在（例如 G1 的 `DRV_PWM_Start`、G2 的 `BSP_OTA_Process`）
3. 反汇编抽查关键点：ATMR `MOE` 位未被置 1、ADC DMA 使能位未被置 1、`DRV_DEVICE_MIGRATION_GATE` 渲染值与预期一致
4. 三项齐全才算静态腿通过；**任一项不通过 → 不进实板**

### 6.3 证据归档位置

- 证据主体放在 **aurora 仓** `docs/bringup/Gxx-名称/`（与 `docs/46-...` 同树，便于与路线图对照评审）
- 目标仓 `mppt-charger-300w/` 内只留一行指针文件指向上述路径，**不重复存放二进制证据**
- 每个 Gate 结束写 `RESULT.md`，状态只允许 `PASS` / `IN_PROGRESS` / `BLOCKED`；缺任何一条腿一律 `IN_PROGRESS`

---

## 7. 外部方案审议记录

对象：`C:\Users\zhuxi\.cursor\plans\g0-g5_bring-up_plan_5863fe07.plan.md`（另一 AI 的 G0–G5 方案，210 行）。

### 7.1 ✅ 同意并保留原方案共识（9 项）

| # | 项 | 说明 |
|---|---|---|
| 1 | G0 先冻结 PinMap 再写代码 | 与 46 号文档一致 |
| 2 | 单路异步 Boost 红线、PA14 保持 GPIO LOW | 与原理图一致 |
| 3 | PVD 只作启动资格、不作运行 Fault 源 | 与 aurora `#error` 护栏一致 |
| 4 | 稳定窗口中途掉落必须重启完整窗口 | 一致 |
| 5 | 看门狗放在 G2 最后一步 | 一致 |
| 6 | ADC 先单通道原始码（G3）再上 DMA（G4） | 一致 |
| 7 | DMA ISR 只置事件、禁止查表/MPPT/printf | 一致 |
| 8 | 标定必须实测拟合、不得用理论值充数 | 一致 |
| 9 | `/code_wrt` 两阶段写法与注释风格 | 已并入 §6.1 |

### 7.2 ⬆ 采纳并补充（9 项，对方补到了我漏的）

| # | 对方提出 | 我的补充落点 |
|---|---|---|
| 1 | `MIGRATION_GATE` 编译期门禁宏 | 采纳，**落点在驱动层 `drv_device.h`** 且与 `DRV_DEVICE_GATE_*` 语义严格分离 → §1.1 |
| 2 | 记录并上报 Reset Cause | 采纳，**扩到 §0.8 全 8 个标志 + 读后清除 + `PVDRSTFLG` 告警** → G2-C4 |
| 3 | IWDT TIMEOUT 1000 ms / GRACE 500 ms | 采纳，**补 WINDOW 100 ms，并对齐 aurora `app_config.h:336-338` 原值** → G2-D2 |
| 4 | 1 ms 节拍用示波器测探针脚 | 采纳，**探针脚改为 PB11/LED_FAULT 并加 `MIGRATION_GATE==2U` 编译隔离** → G2-B3 |
| 5 | MAP / 反汇编做静态验证 | 采纳，**升级为每个 Gate 的三条静态腿判据** → §6.2 |
| 6 | 目标工程无 Host CI | 采纳，**明确 Gate 静态腿改以 Keil + MAP 为准，aurora `test/` 不随迁** → §6.2 |
| 7 | TX 缓冲不足时丢整行 | 采纳，**补「递增具名溢出计数 `debug_tx_drop_count`」** → G2-C3 |
| 8 | PVD 阈值/滤波枚举取值 | **数值确认正确**（`PVDTHSEL=010` → 2.7/2.8 V；50 µs@64 MHz = 3200 cycles），但改为**保留 aurora `#if/#error` 推导链**而非硬写枚举 → G2-A1 |
| 9 | 故障注入方式（卡死主循环 / 停 1 ms 任务） | 采纳，**补「ISR 只置票不喂狗」不变量，否则注入测不出来** → G2-D4 |

### 7.3 ❌ 反驳（8 项）

| # | 对方结论 | 反驳依据 |
|---|---|---|
| 1 | 「G32 只有一个 USART，Debug 必须与 `BSP_OTA` 共用 PA10/PA11」 | **事实错误**。G32F031 有两个独立串口外设：`USART`@0x40003800（`USART_IRQn=27`）与 `UART`@0x40003C00（`UART_IRQn=28`），startup 文件两个向量都在。数据手册表格 9 明确 **PB7=UART_TX@AF0、PB8=UART_RX@AF0**。共用一路会让调试口与 OTA/产品协议抢占同一外设，是自找的耦合 → §0.1 |
| 2 | 1 ms 探针放 **PA12/LINK** | **自相矛盾**。同一方案里 LINK 是通信/使能类功能输出，1 kHz 翻转会驱动真实网络；且对方自己也写了「禁止占用功率脚」，却把探针放到功能输出脚上 → 改 PB11 |
| 3 | `MIGRATION_GATE` 定义在 `app_hw_config.h` | **分层违规**。`app_hw_config.h` 属 `app/`，而门禁要在 `driver/` 里做裁剪，会造成 driver 反向依赖 app → 落到驱动层 `drv_device.h`（该 app 头文件后续已整体删除） |
| 4 | 新增 `bsp_pvd.c/.h`、`bsp_debug.c/.h`、`bsp_wdg.c/.h` | **复用鬼影文件名**。目标 `Objects/` 里残留同名 `.o`/`.crf`（源文件早已不存在），沿用旧名会与陈旧产物混淆；且与「整体搬 aurora 两层架构」的决策冲突 → 用 `drv_system.c` / `drv_debug_uart.c` / `drv_watchdog.c` |
| 5 | G2 主循环保留 `BSP_OTA_Process()` | **台架风险**。台架阶段留一条未验证的跳 Bootloader 路径，一次误触发就丢现场 → G1 删除、G2 由门禁关闭 |
| 6 | 板级参数/标定写进 `app_hw_config.h` | **双真源**。板级标定的唯一真源在驱动层（现为 `drv_board.h`），两处并存必然漂移 → G5-8 明令禁止 |
| 7 | G0 基线对照表：「现 Application：PA8=PV_U、PB0=BST_U、**PB1=COMP2−**」 | 前两项对，**`PB1=COMP2−` 是错的**。目标 `app_hw_config.h` 里没有这一行；且数据手册表格 9 中 PB1 的模拟功能是 `ADC_IN4 / COMP1_INN / VREF+`，**COMP2_INN 在 PB2**。抄错基线会直接污染 G0 冻结 → G0-2 已用逐字核对表替换 |
| 8 | 用现有 `DRV_ADC`（GTMR TRGO + 5 通道 EOS）当作 G4 已具备 | **不成立**。无 DMA 双缓冲、无 producer/consumer 序号、无 overrun 计数、通道映射与 aurora 冲突 → §4 开头「不得冒充」 |

### 7.4 双方都漏、本版新增（6 项）

| # | 项 | 落点 |
|---|---|---|
| 1 | 两工程 DDL 库版本分叉（PMU/RCC/FLASH/ATMR/UART 共约 20 处 API 差异） | §0.2，决策：目标 DDL 为唯一真源，只改 aurora 侧 5 行 |
| 2 | `ATMR_CR1_UDISEN` bit1↔bit2 静默语义冲突 + `InitTypeDef` 字段顺序不同 | §5 首行，G6 必须重写 `drv_pwm.c` |
| 3 | 目标 uvprojx **缺 `g32f031_ddl_dma.c`** → G4 必链接失败 | G4-1 |
| 4 | aurora `drv_watchdog_init()` **缺 IWDT APB 时钟使能** | §0.5 / G2-D1 |
| 5 | aurora `BOARD_FLASH_PAGE_B 0xFE00` 与目标 **OTA 标志页重叠** | §0.4 / G0-3 |
| 6 | PVD **100 mV 迟滞 + ±2.5% 容差**；`RCC->RSTCSR` 是 **8** 个标志不是 4 个 | §0.7 / §0.8 |

---

## 8. Review（每批完成后回填）

### 批次 1 Review

**2026-09-01 外部 AI 复审**（逐条核实后裁定，详见 RESULT.md §11）：

**采纳（真实缺陷/遗漏）：**
- [x] **P0** `drv_device.h` 的 `DRV_DEVICE_GATE_PINMAP_REVIEWED` 当前 `(1U)` 但注释自认"待 Jovi 签字"——违反该文件自己声明的"证据归档后才允许置 1"语义，改回 `(0U)`（已核实：全工程零引用，纯语义修正）
- [x] **P0** G00 README 补 `G0-4 冻结中断优先级表`（todo 170 行早已计划、未交付；值与 `drv_irq_configure_priorities()` 一致：COMP0/COMP1_2_3/ATMR_BRK=0，DMA_CH1=1，ADC/SysTick=2，USART/UART=3）；同时消解编号冲突（G00 README 的"G0-4"目前是拓扑红线，需改其一）
- [x] **P0** G00 README §1 标题"唯一真源 = `driver/inc/board_config.h`"已过期（该文件已删，真源现为 `drv_io.h` / `drv_debug_uart.h` / `drv_device.h`）
- [x] **P1** legacy `drv_comp.c` COMP2 负端用 `DDL_COMP1_INPUT_MINUS_PB1`，而 G0 冻结表（todo 185 行）与数据手册均裁定 **COMP2_INN = PB2、PB1 = ADC_IN4/BST_U**。G1 无 Init 不触雷，G7 前必须修；按 COMP0 极性同等待遇记入 `drv_comp.h` 注释 + RESULT + doc46 §23
- [x] **P1** 三个 M0+ 占位 handler 补 `drv_io_force_power_safe()`（保留的目的是"日后移植复用"，模板本身应安全；死代码零镜像成本）
- [x] **P1** `g32f031_int.c` 三个外设桥接按门禁条件编译（ADC≥3 / COMP≥7）：G01 §4.5 的 208B/102B 死重延期理由（"APP 层不得含 board_config.h"）已随该文件删除而失效，`main.c` 现在就 include `drv_device.h`
- [x] **P2** legacy `drv_pwm.c` 整文件 `#if DRV_DEVICE_MIGRATION_GATE >= 6U`，`bsp_ota.c` 的 `DRV_PWM_Stop` 成对裁剪（G6 必须先改成单端再抬门禁）
- [x] **P2** PinMap 宏↔`GPIO_BSRR_BSn` 编译期 `#error` 检查（`drv_io.c`）
- [x] **P2** `drv_watchdog.h` 补 TIMEOUT_MS=1000 / STARTUP_GRACE_MS=500 / WINDOW_MS=100

**反驳（不采纳，附证据）：**
- GPIO 按脚反复 Unlock/Init/Lock：与 aurora 参考 `gpio_output()` **逐字同构**，直线初始化代码无并发窗口，G32 LOCK 可再开；改批处理偏离已验证参考、零安全收益
- "PVD 全套留在 G1 镜像"：uvprojx `OneElfS=1` 已核实，未引用函数各自成段被 GC，镜像无 PMU 代码（重跑 MAP 后确认）；且 G2 下一批即启用 PVD，加 `#if` 再拆是空转
- "APP 已能看见板级宏"：两层架构中 app include driver 头即设计本身（`main.c` 必须见 `drv_device.h` 才能 `drv_device_config()`）；可机械强制的是"driver 不 include app"方向，已脚本验证。`main.c` 现状只用函数与 `APP_RUN_LED_HALF_PERIOD_MS`
- `drv_device.c` 无条件 `#include "bsp_ota.h"`：`#if DRV_DEVICE_FEATURE_OTA_ENABLE` 分支开启时需要该声明，常规做法非问题
- `__attribute__((noreturn))`：`uGnu=1` 已核实，AC5 合法编译，且带 host-mock 双分支结构；改 `__declspec` 是无收益 churn

**已确认与我方记录一致（非新发现）：** 批次1未 PASS（G0 签字/通断、G1 实板腿、Keil 证据落后三代）——本轮一直在报；208B/102B 死重为 G01 §4.5 原记录；两代配置共存为 Jovi 裁决；`drv_pwm.c`/`drv_comp.c` 未做 code_zl 全整理（保名替换 + 令牌流不变证明，全整理留 G6/G7）。

### 批次 2 Review
（待填）

### 批次 3 Review

**2026-09-02 首轮审核**（H1/H2/M1–M4 六条，详录于会话；H1=IWDT 40kHz、H2=G3-T01 七字段+vdd_mv、M1=NTC 表两端、M2=adc_start 位置、M3=FATAL 静默、M4=目标仓判据指针）。

**2026-09-03 修复轮复核（第三轮审核，工作流 18 agents + 主线程独立验证）：**

修复符合度（10/10 处置：8 fixed + 1 partial + 1 本轮未触及）：

| 旧发现 | 结论 | 关键证据 |
|---|---|---|
| #1 IWDT 顺序+LSI | ✅ fixed | 逐行对齐手册 §15.1.4.3 九步；AXF 反汇编级复核（0xCCCC→0x5555→PSC=4→PVU→RLR=512→RVU→0xAAAA）；LSI 失败先于 0xCCCC 可安全挂死 |
| #2 GATE=1/2 编不过 | ✅ fixed | 五门全 0E/0W（Code 1932/7848/9064/9612/10284）；围栏与唯一使用者同宽，非巧合 |
| #3 ADC-start 复位环 | ✅ fixed（设计接受） | 三层失败语义分层正确：pre-IWDT 永挂 / post-0xCCCC ≤32s / post-config ≤1s，注释已声明 |
| #4 FATAL 静默回归 | ✅ fixed | 三处 FATAL 全改裸字节 hang_after_uart，不受 DEBUG_ENABLE 裁剪 |
| #5 PVD 竞态+静默 | ⚠️ partial | 2-tick 地板已修（竞态消除）；静默 WFI（drv_device.c:61-67）保留为已知项 |
| #6 NTC 3 LSB 余量 | ❌ unfixed | 本轮未触及，V0.3.0 §12⑬ 已落档 |
| #7 VBG 出厂字窗 | ✅ fixed | [1218,1300] 精确对应 VBG 1.19–1.27V；0/0xFFFFFFFF/花码全拒 |
| #8 vdd/vbg 钳位 | ✅ fixed | 三窗串联无漏放无误杀；int64 无溢出 |
| #9 STOP 收尾 | ✅ fixed | 比手册 §19.4.12 更严（双清）；残余理论窗口 ~0.2–0.8µs 需 ADC 内核 >31ms 卡死，已收敛 |
| #10 G3 多行日志 | ✅ fixed（再文档化） | 6×~70B≈420B>256B 理由经计算为真；三处注释+README 一致 |

存活项（全部 低/info 级，无功能缺陷）：LSI ±10% 注释无出处+#error 守卫只按标称（低，调参隐患）；VBG 三窗注释公式错/结果窗下界 2500 死码（低）；vdd_mv=0 三成因文档只列两（低）；drv_device.c:27 漏 RVU 路径（info）；drv_system.c:295 "约1ms" 注释失配（低）；等待循环末轮假失败窗（info）。

事实更正：复位默认 PSC=0x7(/256)+0xFFF → 32s（手册 §15.4.2 实取）——上一轮工作流推断的"500ms"系 STM32 假设，错误。DDL_IWDT_PSC_64=0x4（非 0x2，PSC_2 是位名非数值），固件写入值正确。

红线回归全过：drv_pwm.o 0 字节、ATMR GC、PA14 永久 GPIO 低、POWER_OUTPUT_ALLOWED=0、分层单向、uvprojx 仅 +2 文件条目、DMA_CH1 桥名咬合。
