# G1 最小安全工程 — 验收记录

- **门禁状态：`IN_PROGRESS`**
- 目标工程：`D:\work\mppt-charger-300w\Application`
- 门禁宏：`DRV_DEVICE_MIGRATION_GATE = 1U`（`driver/inc/drv_device.h`）
- 记录时间：2026-09-01

> **阅读提示**：本文 §2~§7 是 G1 交付当时的**原始证据**（含当时的文件名、函数名与 MAP 符号地址），
> 为保持证据可追溯**不作改写**。此后进行过一次代码风格重构（文件/函数/宏改名、删除聚合头），
> 当前工程的真实文件与符号名以文末 **§8 代码风格重构附录** 为准。

> 状态判定依据：代码腿已完成，静态腿已通过（最新 Rebuild 证据见 §11.6，0 Error / 0 Warning）；
> **实板腿 G1-10 未执行**。按路线硬规则，实板腿未完成不得记 `PASS`。

---

## 1. 门禁定义

G1 = 目标工程上电后处于**可证明的安全静止态**：

| 要求 | 落实方式 |
|---|---|
| 功率通路全关 | PA15/GLC、PA14/GHC、PA13/RELAY、PA12/LINK 均为 GPIO 推挽输出低 |
| 不使能任何功率外设 | ATMR、ADC、COMP、OPA、USART 一律不 Init、不 Start |
| GLC 不得出现未知脉冲 | `drv_io_init()` 排在 `SysClk_Config()` 之后、任何外设 Init 之前 |
| 有运行期活体证明 | RUN 灯 1 Hz 闪烁，同时覆盖时钟、1 ms 节拍中断、GPIO 输出三条链路 |
| 异常路径也断功率 | `HardFault_Handler` 先调 `drv_io_force_power_safe()` 再自旋 |

---

## 2. 代码改动清单

### 2.1 新建文件（6 个）

| 文件 | 作用 |
|---|---|
| `driver/inc/board_config.h` | 板级参数唯一来源；新增 `BOARD_MIGRATION_GATE` 与 Flash 分区越界 `#error` |
| `driver/inc/drv_io.h` / `driver/src/drv_io.c` | 功率 GPIO 安全态、LED、继电器、LINK |
| `driver/inc/drv_system.h` / `driver/src/drv_system.c` | 时钟、1 ms 节拍、IRQ 优先级、PVD 供电资格、复位源 |
| `driver/inc/driver.h` | Driver 层聚合入口，APP 唯一依赖 |

### 2.2 修改文件（5 个）

| 文件 | 改动 |
|---|---|
| `app/src/main.c` | **删除** `BSP_ADC_Start` / `BSP_PWM_SetFrequency` / `BSP_PWM_SetDuty(500)` / `BSP_PWM_SetComplementary(1)` / `BSP_PWM_Start` / `BSP_OTA_Process` / `BSP_PWM_IsBraked`；改为 RUN 灯 1 Hz 闪烁 |
| `app/src/g32f031_int.c` | `SysTick_Handler` → `drv_time_tick_isr()`；`HardFault_Handler` 增加断功率；**删除** M0+ 不存在的 `MemManage`/`BusFault`/`UsageFault` 三个 Handler（→ 已于 2026-09-01 按 Jovi 裁决补回，见 §10.2） |
| `driver/src/g32f031_device_cfg.c` | `Device_Config()` 重排为安全顺序 + 门禁分支；**删除** 空壳 `RCM_PeripheralClkConfig`/`GPIO_Config` 与 M0+ 空操作 `NVIC_Config`；`SysClk_Config()` 删除 `DDL_Init1msTick` |
| `driver/inc/g32f031_device_cfg.h` | 同步三个函数的注释契约 |
| `app/inc/app_hw_config.h` | 加废弃横幅并列出与冻结基线的三处冲突；**不改任何取值** |
| `Project/MDK/IAP_Application.uvprojx` | `driver` 组加入 `drv_io.c` / `drv_system.c`；IROM1 Size `0xee00` → `0xea00`；`MiscControls` 加 `--diag_error=warning` |

### 2.3 上电调用链（gate = 1）

```
main()
 └─ Device_Config()
     ├─ SysClk_Config()              HSI 64MHz + Latency3 + SystemCoreClockUpdate
     ├─ drv_io_init()                功率 GPIO 安全态（最先，早于任何外设）
     ├─ drv_system_init()            1ms SysTick（含 TICKINT）
     ├─ drv_irq_configure_priorities()
     ├─ [gate>=2]  drv_system_wait_for_supply_stable()   ← 本阶段不编译
     ├─ [gate>=3]  BSP_ADC_Init()                        ← 本阶段不编译
     └─ [FEATURE_OTA] BSP_OTA_Init()                     ← 预留，默认不编译
 └─ while(1) RUN 灯 1Hz（含 BSP_OTA_Process 预留点）
```

---

## 3. 已完成的静态检查（无需工具链）

| 项 | 结论 | 证据 |
|---|---|---|
| 8 处 `drv_*` 调用点与头文件声明逐一匹配 | ✅ | 参数、返回类型全部一致；`drv_system_wait_for_supply_stable` 返回 `bool`，调用处已 `(void)` 转换 |
| `drv_system_wait_for_supply_stable` 是否需要先调 qualifier_init | ✅ 不需要 | 函数内部第一步即调 `drv_system_supply_qualifier_init()` |
| `PVD_IRQHandler` 是否保持 weak | ✅ | `startup_g32f031.s:153` 导出 `[WEAK]`，`g32f031_int.c` 未定义同名函数 |
| `MemManage`/`BusFault`/`UsageFault` 是否可达 | ✅ 不可达 | M0+ 向量表无对应条目，startup 也未导出弱符号 → 当时作为死代码删除；**2026-09-01 按裁决补回空定义，仍不可达，见 §10.2** |
| OPA 是否属于 ADC 采样链 | ✅ 不属于 | OPA0/OPA1 在 PA0~PA5 作 COMP 前端；冻结的 6 通道 ADC 图为 PA8/PA9/PB0/PB1/PB12/PB5 + PV_I → OPA 随 COMP 归 G6 |
| 孤儿目标文件是否会被链入 | ✅ 不会 | `bsp_debug.o` / `bsp_pvd.o` 源文件全仓库不存在，且 uvprojx 零引用（inert 残留） |

---

## 4. 构建记录

### Rebuild #1（Jovi 执行，ARMCC V5.06u6）

```
..\..\driver\inc\driver.h(9): error: #9-D: nested comment is not allowed
   *   - 只有 driver/src/*.c 允许出现 DDL_ 前缀与寄存器操作
```

3 Error(s), 0 Warning(s) — 三条报错同一个根因：`driver.h:9` 注释里写了 `driver/src/*.c`，
`/` 紧跟 `*` 构成嵌套注释起始。受影响的三个文件正是包含 `driver.h` 的
`g32f031_device_cfg.c` / `main.c` / `g32f031_int.c`。

**已修复**：该行改为「只有 driver 层的 .c 文件…」。并对全部 11 个新建/修改文件做了
块注释状态机扫描（逐字符跟踪 `/*` `*/` `//` 状态），确认无第二处嵌套注释、无未闭合块注释。

### Rebuild #1 的两个重要收获

1. ✅ **`--diag_error=warning` 项目级设置是安全的**，§4 原先记录的风险已排除。
   `bsp_analog.c` / `bsp_pwm.c` / `bsp_adc.c` / `bsp_ota.c` 与全部 11 个 DDL 库源文件
   **均报 0 warning**，legacy 代码本身是干净的，无需把该选项下移到 File Option。
2. ✅ `drv_io.c` 与 `drv_system.c` **各自 0 error / 0 warning 通过编译**（二者不包含
   `driver.h`，未受本次错误波及）。这意味着 700 余行移植代码的语法、以及全部 DDL 符号
   与 4 处 PMU API 改名均已被真实工具链验证通过。
   包含 `driver.h` 的三个文件也仅报这一条诊断，其余部分同样干净。

### Rebuild #2（Jovi 执行）→ 静态腿 **PASS**

镜像：`Objects/G32F031/IAP_Application.axf`，`Listings/G32F031/IAP_Application.map`（2026-09-01 15:35）

#### 4.1 编译链接 ✅

0 error / 0 warning（`--diag_error=warning` 生效）。

#### 4.2 MAP 符号核查 ✅

**应不存在的 12 个符号，全部被链接器显式移除**（符号表 0 命中 + `Removing ...` 各 1 条）：
`BSP_PWM_Start`、`BSP_PWM_SetDuty`、`BSP_PWM_SetFrequency`、`BSP_PWM_SetComplementary`、
`BSP_PWM_Init`、`BSP_PWM_IsBraked`、`BSP_ADC_Start`、`BSP_ADC_Init`、`BSP_OTA_Process`、
`BSP_OTA_Init`、`BSP_OPA_Init`、`BSP_COMP_Init`

**应存在的符号，全部就位**：

| 符号 | 地址 | 大小 |
|---|---|---|
| `drv_io_init` | `0x000016B1` | 106 |
| `drv_io_force_power_safe` | `0x00001689` | 32 |
| `drv_io_set_leds` | `0x00001729` | 32 |
| `drv_irq_configure_priorities` | `0x0000174D` | 68 |
| `drv_system_init` | `0x00001791` | 52 |
| `drv_time_now_ms` | `0x000017D5` | 6 |
| `drv_time_tick_isr` | `0x000017E1` | 10 |
| `Device_Config` | `0x0000146D` | 20 |
| `main` | `0x00001841` | 38 |

> `drv_io_set_relay` / `drv_io_set_link` 被链接器移除——G1 无调用者，属预期，非缺陷。

**地址与容量** ✅

| 区段 | 实测 | 上限 | 余量 |
|---|---|---|---|
| `ER_IROM1` | `0x00001000` ~ `0x000018E0`（Size `0x8E0`） | `0xEA00`（至 `0xFA00`） | **57,632 B** |
| `RW_IRAM1` | `0x20000000` ~ `0x20000448`（Size `0x448`） | `0x2000` | 7,096 B |

最高 ROM 地址 `0x000018E0` **远低于参数页 A 起始 `0x0000FA00`**，不存在侵入风险。
Total RO 2,248 B / RW+ZI 1,096 B。

#### 4.3 「MOE 未置 1、ADC DMA 未使能」——以**不存在证明**替代反汇编抽查 ✅

镜像仅链入 11 个目标文件。相关 DDL 模块**整体未参与链接**，即对应寄存器不存在任何写入路径：

| DDL 模块 | 链接结果 | 结论 |
|---|---|---|
| `g32f031_ddl_atmr.o` | **完全未链入** | ATMR 全部寄存器无写入路径 → `MOE` **不可能被置 1** |
| `g32f031_ddl_adc.o` | 链入但 **Code = 0** | ADC 寄存器无写入路径 → DMA 使能位**不可能被置 1** |
| `g32f031_ddl_comp0.o` / `comp1.o` / `opa.o` / `usart.o` / `gtmr.o` | **完全未链入** | 比较器、运放、串口、触发定时器均无写入路径 |
| `g32f031_ddl_gpio.o` | 链入 546 B | 唯一链入的 DDL 外设模块，符合"纯 GPIO 工程"预期 |
| `g32f031_ddl_utils.o` | **完全未链入** | 证明 `DDL_Init1msTick` 删除已生效 |

此证明强于反汇编抽查：抽查只能说明"检查到的位置未置位"，而模块整体缺席说明**不存在任何置位路径**。

#### 4.4 追加核查（超出原计划三项）✅

| 项 | 结果 |
|---|---|
| 全镜像 `NVIC_EnableIRQ` 调用 | **0 处**。唯一调用者 `BSP_COMP_Init` 已被移除，`__NVIC_EnableIRQ` 本体亦被移除（`Removing bsp_analog.o(i.__NVIC_EnableIRQ), (24 bytes)`）→ 无任何中断被使能 |
| `PVD_IRQHandler` 归属 | `startup_g32f031.o(.text) 0x000010E7` = **弱桩**，`drv_system.c` 未定义同名函数，红线守住 |
| EINT 相关符号 | 仅 `EINT0_1` / `EINT2_3` / `EINT4_15` 三个 startup 弱桩，无初始化代码 → PVD 所在 EINT line 16 未配置 |
| `SCB->VTOR`（IAP 关键） | `SystemInit` 指令级确认：`MOVS r0,#1` → `LSLS r0,r0,#12`（= `0x1000`）→ `STR r0,[0xE000ED00 + 8]` = **VTOR = 0x1000** ✅ |
| `SysTick->CTRL` | `0x17C0: MOVS r0,#7 / STR r0,[0xE000E000+0x10]` = `CLKSOURCE\|TICKINT\|ENABLE` → **1 ms 中断真正开启**（对比修复前 `DDL_InitTick` 不置 TICKINT） |
| `SysTick_Handler` 实体 | `PUSH {r4,lr}` → `BL drv_time_tick_isr` → `POP` ✅ |
| `drv_io_init` 引脚配置 | `Mode = 2 = DDL_GPIO_MODE_OUTPUT`（非 `ALTERNATE = 3`）、`OutputType = 0 = PUSHPULL`、`Pull = 0`、输出值 `0`，作用于 PA15/PA14/PA13/PA12 → **GLC 为真推挽输出低，未被配成 AF** |
| `drv_io_force_power_safe` 实体 | `GPIOA->BSRR(@0x20) = 0xF0000000` = 复位 PA15/14/13/12 ✅ |

> 说明：`drv_system_init` 反汇编中 SysTick 优先级出现 `3`，来自 CMSIS `SysTick_Config()`
> 内部默认值 `(1 << __NVIC_PRIO_BITS) - 1`（M0+ 为 2 位 → 3），随后由
> `drv_irq_configure_priorities()` 覆盖为 2。属 CMSIS 惯用写法，非缺陷。

#### 4.5 遗留观察（非缺陷，不阻塞门禁）

`bsp_adc.o`（136 B Code + 52 B RW + 42 B ZI）与 `bsp_analog.o`（72 B Code + 8 B RW）仍被链入，
原因是 `g32f031_int.c` 的 `ADC_IRQHandler` / `COMP0_IRQHandler` / `COMP1_2_3_IRQHandler`
三个桥接函数被 startup 向量表引用，属常驻。

- **安全性**：因全镜像无任何 `NVIC_EnableIRQ`、且 ADC/COMP 时钟未开、外设未初始化，
  这三个 ISR **运行期不可能被触发**，不构成 G1 违规。
- **代价**：约 208 B Flash + 102 B RAM 的死重。
- **为何不在本批次消除**：需把三个桥接函数按门禁条件编译，而 `g32f031_int.c` 属 APP 层，
  依红线不得包含 `board_config.h`；彻底解法是把中断桥接文件下沉到 Driver 层。
  该动作影响面大于收益，留待 G3 接入 ADC 时一并处理（那时 `ADC_IRQHandler` 本就需要）。

> → **2026-09-01 更新**：延期理由已随 `board_config.h` 删除失效，桥接已按门禁裁剪（ADC≥3 / COMP≥7），死重消除，见 §11.4。

---

## 5. 待执行：实板腿

- [ ] **5.1** 烧录后示波器确认 PA15/GLC 全程为低（不得用 PA12/LINK 作探针，见 G00）
- [ ] **5.2** RUN 灯稳定 1 Hz 闪烁 → 证明 64 MHz 时钟与 1 ms 节拍正确
- [ ] **5.3** 万用表确认 RELAY 未吸合

> G0 的台面腿（Jovi 基线签字、导通检查）尚未闭环，但 G1 为纯代码 + 静态验证，不受阻；
> **G2 上电前 G0 台面腿必须先闭环**。

---

## 6. 改动过程中发现并报告的疑似缺陷

按 `/code_wrt` 规则，发现即报告；其中仅与本次门禁直接相关者已随手修正，其余不改行为。

| # | 缺陷 | 处置 |
|---|---|---|
| 1 | **上电即 50% 占空比 + 互补输出**：原 `main.c` 顺序调用 `SetDuty(500)`→`SetComplementary(1)`→`PWM_Start()`，且 `PA14/GHC` 被配成 `CH0N`。单路异步 Boost 无高侧 MOS，此为最危险的一处 | 已删除（属 G1 门禁定义范围内） |
| 2 | **SysTick 双主人**：`DDL_InitTick()`（`g32f031_ddl_utils.h:210`）配置 SysTick 但**不置 `TICKINT`**，所以既有 `SysTick_Handler` 从未被调用；`drv_system_init()` 又会重配同一外设 | 已删除 `SysClk_Config()` 中的 `DDL_Init1msTick`，SysTick 交由 `drv_system_init` 独占。全仓库无 `DDL_mDelay`/`DDL_uDelay` 调用者，删除无副作用 |
| 3 | **`NVIC_SetPriorityGrouping(3)` 是死代码**：`core_cm0plus.h:732` 将其定义为 `(void)(X)`，M0+ 无优先级分组寄存器 | 已随 `NVIC_Config()` 一并删除，由 `drv_irq_configure_priorities()` 取代（该函数刻意不调分组接口并注明原因） |
| 4 | `app_hw_config.h` 与冻结基线冲突三处：`APP_PWM_DEADTIME_TICKS`、5 通道 `APP_ADC_CHANNEL_COUNT`、注释中的 `PA14(ATMR_CH0N)` | **仅加废弃横幅，不改取值**（改动会影响仍在编译的 `bsp_adc`/`bsp_pwm` 行为，属 G3/G6 范围） |
| 5 | `drv_io_force_power_safe()` 原实现在 `drv_io_init()` 之前被调用时，GPIOA 时钟仍门控 → 写外设可能触发二次异常；M0+ 无 BusFault，会直接升级为 Lockup，而 Bring-up 阶段 `LOCKUPRSTEN=0` 意味着静默死机 | 函数内自行确保 GPIOA 时钟已开，使其可在任意上下文调用 |
| 6 | `Objects/G32F031/` 残留 `bsp_debug.o`、`bsp_pvd.o`，源文件全仓库不存在 | 经 Jovi 授权**已删除**（含 `.crf`/`.d` 共 6 个文件）；均为可再生构建产物，uvprojx 零引用 |
| 7 | **我自己引入的构建错误**：`driver.h:9` 注释内 `driver/src/*.c` 构成嵌套注释，导致 Rebuild #1 三处报错 | 已修复并对 11 个文件做全量块注释扫描；教训已记入 `tasks/lessons.md` |

---

## 7. Jovi 裁决结果

| # | 事项 | 裁决 | 落实 |
|---|---|---|---|
| 1 | `aurora_irq_state_t` → `drv_irq_state_t`、include guard 去 `AURORA_` 前缀 | ✅ 批准 | 保持现状；已确认 `driver/`、`app/` 下无 `aurora_` 标识符残留（`board_config.h` 中两处仅为说明 Flash 分区与源工程差异的散文，非标识符） |
| 2 | 蓝牙与后期 OTA 需**功能预留**，不可简单删除 | ✅ 采纳 | 新增与门禁**正交**的一组开关：`BOARD_FEATURE_OTA_ENABLE` / `BOARD_FEATURE_BLUETOOTH_ENABLE`（默认 0）。`BSP_OTA_Init()` 调用与 `bsp_ota.h` 声明均已就位，置 1 即可编译通过；`main.c` 主循环留有 `BSP_OTA_Process()` 预留点。两者共用 USART PA10/PA11，同时置 1 会触发 `#error`；蓝牙置 1 会触发 `#error` 提示尚未实现 |
| 3 | 两个孤儿目标文件删除 | ✅ 执行 | 见 §6-6 |
| 4 | `--diag_error=warning` 置于项目级 | ✅ 风险已排除 | Rebuild #1 证明 legacy 与 DDL 源文件均 0 warning，见 §4 |

> **为什么功能预留不复用 `BOARD_MIGRATION_GATE`**：门禁宏回答"移植进行到第几阶段"，
> 功能开关回答"某个后期功能是否已接入"。蓝牙/OTA 的落地时间由产品需求决定，
> 与 G0~G5 路线无因果关系；若挂到门禁上，将来为了开蓝牙就得抬门禁值，
> 等于用安全门禁去表达功能开关，是明确的语义污染。

---

## 8. 签字

| 腿 | 责任人 | 状态 |
|---|---|---|
| 代码 | Claude | ✅ 完成 |
| 静态（Keil Rebuild + MAP + 反汇编） | Jovi 编译 / Claude 核查 | ✅ **PASS**（见 §4，Rebuild #2） |
| 实板 | Jovi | ⬜ 待执行（§5 三项） |
| 归档 | — | ⬜ 待实板腿闭环后补 |

> **G1 门禁总状态仍为 `IN_PROGRESS`**：代码腿与静态腿已闭环，实板腿未执行。
> 依路线硬规则，实板验证未完成一律不得记 `PASS`。

---

## 8. 代码风格重构附录（G1 归档之后）

Jovi 提出代码风格要求后对目标工程做了一次**纯改名 / 归属重整**，不改变任何行为、取值与硬件访问顺序。
本节列出与 §2~§7 原始证据的差异，供后续 Gate 对照。

### 8.1 文件变动

| 动作 | 原 | 现 |
|---|---|---|
| 改名 | `driver/{inc,src}/g32f031_device_cfg.{h,c}` | `driver/{inc,src}/drv_device.{h,c}` |
| 删除 | `driver/inc/board_config.h` | 配置拆到各驱动自己的 `.h`（见 8.3） |
| 删除 | `driver/inc/driver.h`（聚合头） | 各 `.c` 按需直接 include 所需 `drv_*.h` |
| 删除 | `app/inc/app_hw_config.h` | `APP_ADC_*`/`APP_PWM_*` 迁入 `bsp_adc.h`/`bsp_pwm.h` 并改名 `BSP_*` |
| 改写 | `app/inc/main.h`（原只转发 `g32f031_device_cfg.h`） | 只放 app 全局参数 `APP_RUN_LED_HALF_PERIOD_MS` |
| 新增 | — | 配置骨架头 `drv_adc.h` `drv_board.h` `drv_pwm.h` `drv_comp.h` `drv_flash.h` `drv_debug_uart.h` `drv_watchdog.h`（只含配置宏，无函数声明） |
| 修改 | 4 个 legacy `bsp_*.c` | 聚合头删除后各自补齐真正需要的 DDL include |
| 修改 | `Project/MDK/IAP_Application.uvprojx` | `g32f031_device_cfg.c` 条目 → `drv_device.c` |

### 8.2 函数改名（§4.2 MAP 符号表中的旧名对照）

| 原 | 现 |
|---|---|
| `Device_Config()` | `drv_device_config()` |
| `Device_Reset()` | `drv_device_reset()` |
| `SysClk_Config()` | `drv_sysclk_config()` |

legacy `BSP_*` 函数名**一律未动**（`BSP_ADC_Init` / `BSP_OPA_Init` / `BSP_COMP_Init` / `BSP_PWM_Init` / `BSP_OTA_*`），
按 Jovi 裁决等 G3/G6 各自重写时再改。

### 8.3 宏改名与归属

| 原前缀 | 现前缀 | 落点 |
|---|---|---|
| `BOARD_MIGRATION_GATE` / `BOARD_GATE_*` / `BOARD_POWER_OUTPUT_ALLOWED` / `BOARD_FEATURE_*` | `DRV_DEVICE_*` | `drv_device.h`（跨模块开关，驱动层自持） |
| `BOARD_PIN_*`（GLC/GHC/Relay/LINK/LED）、`BOARD_LED_ACTIVE_LOW`、`BOARD_RELAY_ACTIVE_HIGH` | `DRV_IO_*` | `drv_io.h` |
| `BOARD_PVD_*` / `BOARD_SUPPLY_*` | `DRV_SYSTEM_*` | `drv_system.h` |
| `BOARD_ADC_*`（采集侧）、`BOARD_ADC_GTMR_*` | `DRV_ADC_*` | `drv_adc.h` |
| `BOARD_NTC_*` / `BOARD_ADC_*_DIVIDER_*` / `BOARD_ADC_PV_I_*` | `DRV_BOARD_*` | `drv_board.h` |
| `BOARD_PWM_*` | `DRV_PWM_*` | `drv_pwm.h`（并新增 `#error` 锁死死区为 0） |
| `BOARD_PIN_COMP0_*` / `BOARD_COMP0_FAULT_ACTIVE_LOW` | `DRV_COMP0_*` | `drv_comp.h` |
| `BOARD_FLASH_*` | `DRV_FLASH_*` | `drv_flash.h`（保留页重叠 `#error`） |
| `BOARD_PIN_DEBUG_*` / `BOARD_UART_*` | `DRV_DEBUG_UART_*` | `drv_debug_uart.h` |
| `BOARD_WATCHDOG_*` / `BOARD_MILLISECONDS_PER_SECOND` | `DRV_WATCHDOG_*` | `drv_watchdog.h` |
| `BOARD_PIN_UART_*`（PA10/PA11 产品 USART） | `DRV_DEVICE_PIN_USART_*` | `drv_device.h` |
| `DRIVER_US_PER_MS` / `DRIVER_PVD_THRESHOLD` / `DRIVER_PVD_FILTER_LENGTH`（`.c` 私有宏） | `DRV_SYSTEM_US_PER_MS` / `DRV_SYSTEM_PVD_THRESHOLD_SEL` / `DRV_SYSTEM_PVD_FILTER_SEL` | `drv_system.c` |

### 8.4 重构后的脚本校验（Keil 未重跑，等 Jovi 下次 Rebuild）

| 检查项 | 结果 |
|---|---|
| 嵌套 / 未闭合块注释状态机扫描（26 个 `.c/.h`） | ✅ PASS（0） |
| `BOARD_*` / `APP_*` 前缀零残留（`APP_RUN_LED_HALF_PERIOD_MS` 为合法 app 全局） | ✅ PASS（0） |
| 全部 `#include "..."` 目标可解析 | ✅ PASS（0） |
| 分层单向（driver 不 include app 头 / app 不直接调 `DDL_*`） | ✅ PASS（0） |
| 符号来源闭包（每个 `.c` 的 `DDL_*` / 寄存器 / `*_IRQn` / `extern` 变量均在 include 传递闭包内） | ✅ PASS（9/9 `.c`） |

> **注意**：以上均为静态脚本校验，**不等于**编译通过。§4 的 Keil AC5 Rebuild 证据对应重构**之前**的代码，
> 重构后必须重跑一次 Rebuild 才能延用「0 error / 0 warning」结论；G1 门禁状态仍为 `IN_PROGRESS`
> （实板腿 G1-10 未执行）。

### 8.5 需要 Jovi 裁决的遗留项

> **三项均已在 2026-09-01 裁决完毕，处理结果见 §10。本小节保留原始记录，结论以 §10 为准。**

1. **`drv_device_reset()` 无调用点**：原 `Device_Reset()` 即为空实现且无调用者，本次只改名保留并加注说明，未删除。
2. **`BSP_PWM_DEADTIME_TICKS = 32U` 与拓扑红线冲突**：该值来自原 `app_hw_config.h`，服务仍在编译的 legacy `bsp_pwm.c`。
   本次按"不改变 legacy 行为"原样平移，并在 `bsp_pwm.h` 注释中标明与 `drv_pwm.h` 的 `DRV_PWM_DEADTIME_TICKS (0U)` 冲突、
   以后者为权威。**改这个值属于行为变更，需 Jovi 明确要求。**
   → **该"冲突"判断本身是错的，见 §10.1。**
3. **`g32f031_int.h` 仍声明 `MemManage_Handler` / `BusFault_Handler` / `UsageFault_Handler`**：M0+ 没有这三个异常，
   定义已在 G1 删除，声明是无害残留。本次未动，可在下批顺手删。
   → **裁决为保留并补回定义，见 §10.2。**

---

## 9. 遗留 BSP 模块合并附录（代码风格重构第二波）

> 触发原因：§8 按当时裁决「legacy `bsp_*` 函数名不动」保留了 `bsp_pwm.h`，
> 但骨架头 `drv_pwm.h` 已在同一批建立，**同一个外设出现两个头文件**。
> Jovi 裁定：`bsp_pwm.h`/`bsp_adc.h`/`bsp_analog.h` 与对应 `drv_*.h` 合并，
> 「除了 OTA_ 的驱动文件都改为 drv 的，涉及的函数名称 DRV_ 开头，但是还是大写，引起后续改造的注意」。
> **本节裁决覆盖 §8.5 第 2 条与 §8 的「`BSP_*` 保持不动」结论。**

### 9.1 文件合并

| 原文件 | 归并到 | 依据 |
|---|---|---|
| `driver/inc/bsp_adc.h` | `driver/inc/drv_adc.h` | 同为 ADC 模块 |
| `driver/src/bsp_adc.c` | `driver/src/drv_adc.c`（重命名） | — |
| `driver/inc/bsp_pwm.h` | `driver/inc/drv_pwm.h` | 同为 ATMR PWM 模块 |
| `driver/src/bsp_pwm.c` | `driver/src/drv_pwm.c`（重命名） | — |
| `driver/inc/bsp_analog.h` | `driver/inc/drv_comp.h` | OPA 随 COMP 合并，**与 aurora 一致**（aurora `drv_comp.h` 本身就声明 `BSP_OPA_Init`） |
| `driver/src/bsp_analog.c` | `driver/src/drv_comp.c`（重命名） | — |
| `driver/{inc,src}/bsp_ota.{h,c}` | **不动** | Jovi 明确豁免 OTA |

合并后 `driver/inc` 只剩 `bsp_ota.h` 一个 `bsp_` 头；同一模块 `bsp_`/`drv_` 并存检查 PASS。

### 9.2 函数改名（全大写 `DRV_`，故意保留驼峰后缀）

| 原名 | 新名 | 所属头文件 |
|---|---|---|
| `BSP_ADC_Init` / `_Start` / `_GetRaw` / `_GetAverage` / `_IRQHandler` / `_GetSequenceCount` | `DRV_ADC_*`（同后缀） | `drv_adc.h` |
| `BSP_PWM_Init` / `_Start` / `_Stop` / `_SetDuty` / `_SetFrequency` / `_SetComplementary` / `_IsBraked` | `DRV_PWM_*`（同后缀） | `drv_pwm.h` |
| `BSP_OPA_Init` | `DRV_OPA_Init` | `drv_comp.h` |
| `BSP_COMP_Init` / `_IsActive`、`BSP_COMP2_IsActive`、`BSP_COMP0/2_IRQHandler`、`BSP_COMP0/2_GetEventCount` | `DRV_COMP*`（同后缀） | `drv_comp.h` |
| `BSP_OTA_Init` / `_Process` / `_RequestBootloader` | **不改** | `bsp_ota.h` |

> 大写 `DRV_` 是 Jovi 要求的**可视标记**：提示实现体仍是 legacy、G3/G6 会整体重写，
> 届时才降为小写 `drv_adc_*` / `drv_pwm_*` / `drv_comp_*`。已写入 code_zl V0.2.1 作为规范例外。

### 9.3 两代配置共存（`LEGACY` 分节，值故意不统一）

| legacy 宏（当前代码在用） | 值 | 冻结基线宏 | 值 | 为何不统一 |
|---|---|---|---|---|
| `DRV_ADC_LEGACY_CHANNEL_COUNT` | `5U` | `DRV_ADC_CHANNEL_COUNT` | `6U` | 改 5→6 会同时改缓冲数组尺寸、rank 表、采样时间配置 → 行为变更 |
| `DRV_ADC_LEGACY_SAMPLE_COUNT` | `16U` | —（G3 重定） | — | 滑动平均窗口，随 G3 DMA 双缓冲方案重定 |
| `DRV_ADC_LEGACY_TRIGGER_US` | `100U` | `DRV_ADC_GTMR_PRESCALER/AUTORELOAD` | `63U`/`99U` | 两者数值等价（10kHz）；legacy 代码硬编码 63/99，宏只出现在注释里 |
| `DRV_PWM_LEGACY_FREQUENCY_HZ` | `50000U` | `DRV_PWM_FREQUENCY_HZ` | `50000UL` | 值同，仅后缀不同，合并会触发重定义告警（`--diag_error=warning` → 编译错误） |
| `DRV_PWM_LEGACY_PERIOD_TICKS` | `1280`（算式） | `DRV_PWM_PERIOD_COUNTS` | `1280U` | 同上 |
| `DRV_PWM_LEGACY_DEADTIME_TICKS` | `32U` | —（冻结基线无此项） | — | 互补输出必须有死区，`32U` 是**正当值**；冻结基线是单端输出，压根不设死区项（见 §10.1） |

> ~~`drv_pwm.h` 的 `#error`（死区必须为 0）只约束**冻结基线**那一组~~ →
> **该 `#error` 与 `DRV_PWM_DEADTIME_TICKS (0U)` 已于 2026-09-01 按 Jovi 裁决删除，见 §10.1。**
> 其余五行的"分名而不统一"结论不变。

### 9.4 本次脚本校验

| 检查项 | 结果 |
|---|---|
| 旧前缀零残留（`BSP_OTA_` 除外） | ✅ PASS（0） |
| 块注释无嵌套 / 无未闭合 | ✅ PASS（0） |
| 全部 `#include "..."` 可解析（全仓 185 个 `.h` 索引，53 条 include） | ✅ PASS（0） |
| 分层单向（driver 不 include app 头） | ✅ PASS（0） |
| 同一模块无重复头（`bsp_`/`drv_` 并存） | ✅ PASS（仅 `bsp_ota` 单侧存在） |
| `.uvprojx` 条目 ↔ 磁盘一致；`app/driver` 下无未入工程的 `.c` | ✅ PASS |
| 符号来源闭包（26 个 `DRV_ADC/PWM/COMP/OPA` 引用全部命中头文件声明） | ✅ PASS（26/26） |
| **行为不变性证明**：`git show HEAD:` 原文 → 折叠两代改名映射 → 去注释去 include 令牌流对比 | ✅ PASS（3/3 文件字节级一致：4153 / 2818 / 4147 字符） |

> 令牌流对比是本次最强证据：三个被合并的 `.c` 相对 `HEAD` 的差异**只有标识符改名**，
> 语句序列、DDL 调用顺序、寄存器访问顺序、字面量全部逐字节一致。
> 但这仍**不等于**编译通过——`--diag_error=warning` 下任何告警都是错误，
> **§4 的 Keil 证据现已两代过时，必须由 Jovi 重跑 Rebuild 才能延用「0 error / 0 warning」**。

### 9.5 顺带修正

- `drv_adc.c` 中"见 app_hw_config 通道映射"的失效注释 → 改指 `drv_adc.h` legacy 小节。
- `drv_device.c` / `g32f031_int.c` 的 include 块改回字母序（改名后顺序被打乱）。
- `drv_device.c` 蓝牙预留 `#error` 文案里的 `BSP_BT_Init()` → `drv_bt_init()`（蓝牙是新模块，按 code_zl 用小写）。
- `.uvoptx` 中三条编辑器状态条目同步改名，避免 Keil 打开时报文件缺失。

### 9.6 仍待处理

1. `drv_comp.h` 记录了一处**行为疑点**（未修，仅报告）：`DRV_COMP0_FAULT_ACTIVE_LOW = 1U` 要求故障时输出拉低，
   但 legacy `drv_comp.c` 用的是 `DDL_COMP0_OUTPUTPOL_NONINVERTED`，两者尚未对齐。
   这属于硬件保护极性问题，留待 G6 连同实板强制触发验收一并处理。
   → Jovi 已确认按此处理，验收要求见 `docs/46-...` §23.6。
2. ~~§8.5 第 1、3 条仍未处理~~ → **已于 2026-09-01 裁决并处理完毕，见 §10。**

---

## 10. Jovi 裁决落地（2026-09-01，§8.5 三项结案）

> 三项遗留裁决同步回填至 `docs/46-v0.10.3-新工程分阶段移植与板级验证路线.md` §23。

### 10.1 死区语义纠正 —— §8.5 第 2 条 / §9.3 末行

Jovi 原话：**"互补PWM是要有死区的！！！0的地方删除！！！"**

我原先的判断"`32U` 与拓扑红线冲突、以 `0U` 为权威"**框架就是错的**：死区是**互补输出专有**概念，
单端输出没有上下管换相，因此冻结基线**根本不该有死区配置项**，而不是"把它设成 0"；
legacy 走 CH0/CH0N 互补输出，`32U` 对它是**正当值**。两组不是冲突，是两套拓扑各自的参数。

已执行：

| 动作 | 对象 | 校验 |
|---|---|---|
| 删除 | `drv_pwm.h` 的 `DRV_PWM_DEADTIME_TICKS (0U)` | 零残留 PASS（删前已确认全工程零引用，零消费者变更） |
| 删除 | 其配套 `#if (DRV_PWM_DEADTIME_TICKS != 0U) #error … #endif` | `#error` 已消失 PASS |
| 保留 | `DRV_PWM_LEGACY_DEADTIME_TICKS` | 仍为 `(32U)` PASS |
| 改写 | `drv_pwm.h` 冻结基线 + legacy 两处说明文字 | 明确"单端不设死区项 / 互补 32 tick 正当，勿改 0" |
| 校验 | 对齐列宽未受影响 | `DRV_PWM_AUTOMATIC_OUTPUT`(24) 与 `(64000000UL)`(13) 仍决定列宽，4/4 块 PASS |

**诚实说明**：删掉 `#error` 等于少一道编译期守卫。异步 Boost 红线的**实际**执行点是
`drv_io.h` 的 `DRV_IO_PIN_GHC_UNUSED_*`（PA14 保持普通推挽 GPIO 输出低，物理上不接 ATMR 复用），
最终证据是 G6 示波器实测 PA14 全程 LOW，不依赖任何宏。此说明已写入 `drv_pwm.h` 头部注释。

### 10.2 M0+ 缺失异常 handler 保留 —— §8.5 第 3 条

Jovi 原话：**"这几个函数还是保留吧，函数也保留，免得以后使用！"**

已执行：`g32f031_int.h` 三条声明保留并改写行尾注释（列 32 对齐保持）；
`g32f031_int.c` 在 `HardFault_Handler` 与 `SVC_Handler` 之间**补回三个空定义**，
函数头按 code_zl 格式写明"M0+ 无此异常、向量表无此项、永不被硬件调用、保留供日后移植 M3/M4 复用"。

安全性校验：

| 检查项 | 结果 | 依据 |
|---|---|---|
| 重复符号风险 | ✅ 无 | `startup_g32f031.s` 向量表仅含 `HardFault_Handler`(61/132/134)、`SVC_Handler`(69/137/138)，无这三项 |
| 缺失符号风险 | ✅ 无 | startup 未导出对应弱符号，无人引用 |
| `#177-D unused static function` | ✅ 不触发 | 三者为外部链接；该工程 `--diag_error=warning`，仅 static 未用函数会告警 |
| 三 handler 声明↔定义配对 | ✅ 3/3 | `MemManage`/`BusFault`/`UsageFault` 均声明✓定义✓ |

> 注意 §4 的 `MemManage`/`BusFault`/`UsageFault` 行（第 47、77 行）记的是 G1 当时"已删除"的状态，
> 现已被本节推翻：**声明与定义都在**。

### 10.3 `drv_device_reset()` 保留 —— §8.5 第 1 条

Jovi 原话：**"对的！！"** —— 只改名保留、不删除的处理正确，无需动作。

### 10.4 本轮校验汇总

六项门禁全 PASS（旧前缀零残留 / 块注释状态机 / 53 条 include 对 185 个仓库头全部可解析 /
分层单向 / 同模块无重复头 / `.uvprojx` ↔ 磁盘一致），加第 7 项死区与 handler 专项校验全 PASS，
加对齐校验 4/4 PASS（`drv_pwm.h` 列 47/81/51、`g32f031_int.h` 列 32）。

> **§4 的「0 error / 0 warning」证据现已落后三代**（风格重构 → BSP 合并 → 本轮裁决落地）。
> 该重跑已于 §11.6 完成（UV4 命令行，0 Error / 0 Warning）。G1 门禁仍为 `IN_PROGRESS`（实板腿 G1-10 未执行）。

---

## 11. 外部复审 P0+P1 落地（2026-09-01 第二轮）

> 触发：外部 AI 对批次 1 的全面复审 + Jovi 裁决（`tasks/todo.md` §8 批次 1 Review）。
> 执行方式：subagent-driven（haiku 执行 + sonnet 双阶段审查 + 控制器亲验 diff）。

### 11.1 `DRV_DEVICE_GATE_PINMAP_REVIEWED` 1U→0U

值与注释自相矛盾（置 1 但注释自认"待签字"），违反 drv_device.h 自己声明的"证据归档后才允许置 1"语义。全工程零引用，纯语义修正，无功能影响。

### 11.2 COMP2 负端 PB1→PB2（本轮唯一硬件修正）

G00 冻结表裁定 COMP2_INN = PB2、PB1 = ADC_IN4/BST_U，legacy 实现却按 PB1 配置。已改三处：

- GPIO 模拟模式掩码 `DDL_GPIO_PIN_1|PIN_6` → `DDL_GPIO_PIN_2|PIN_6`（**真正的硬件修正**：不改掩码，PB2 不进模拟模式，COMP2_INN 到不了焊盘）
- 枚举 `DDL_COMP1_INPUT_MINUS_PB1` → `_PB2`——DDL 中两者**同值**（均 `COMP_CR_VNSEL_0`，g32f031_ddl_comp1.h:137/139），改名只为防后人读错
- 函数头/体内注释同步改 PB2 并注明 PB1 归属；drv_comp.h 头注释补 COMP2 冻结引脚段

运行期风险为零：`DRV_COMP_Init` 当前无调用点（G7 才开闸）。顺带记录一处未修观察：legacy `DRV_COMP_Init` 内部把 COMP0/COMP1_2_3 NVIC 优先级设为 1，与冻结表（=0）不一致，属两代配置共存，G7 重写时收敛。

### 11.3 三个 M0+ 占位 handler 补断功率

MemManage/BusFault/UsageFault 保留的目的是"日后移植复用"，模板本身必须安全：函数体首行加 `drv_io_force_power_safe()`，与 HardFault_Handler 同构。M0+ 上仍为死代码，零镜像成本。

### 11.4 向量桥按移植门禁裁剪（消 §4.5 死重）

§4.5 的延期理由（"g32f031_int.c 属 APP 层不得含 board_config.h"）已随该文件删除失效。现按门禁条件编译：ADC 桥 `#if DRV_DEVICE_MIGRATION_GATE >= 3U`，COMP0/COMP1_2_3 桥 `>= 7U`（对齐 doc46：G6=PWM、G7=COMP/Break）。startup 对三个向量均有 `[WEAK]` 兜底（startup_g32f031.s:161/168/169），裁掉强定义不产生链接错误；外设未 Init 且 NVIC 未使能，弱兜底不可达。

MAP 实测（Rebuild 见 §11.6）：`drv_adc.o` / `drv_comp.o` / `drv_pwm.o` 三个目标文件**整体零贡献**——`Removing` 清单含 `DRV_ADC_Init`(436 B) / `DRV_ADC_IRQHandler`(136 B) / `DRV_COMP_Init`(316 B) / `DRV_OPA_Init`(180 B) / `DRV_PWM_Init`(280 B) 及其全部 `.bss`/`.data`/`.constdata`；`ADC_IRQHandler` / `COMP0_IRQHandler` / `COMP1_2_3_IRQHandler` 向量全部回落到 startup 弱桩 `0x000010e7`。三个占位 handler（各 6 B，含断功率调用）同样被 GC，"零镜像成本"由推断变为实证。

> **G7 脚枪**：`drv_device.h` 的 `#if (DRV_DEVICE_MIGRATION_GATE > 5U) #error` 是 G0~G5 范围锁。G6/G7 开闸时若只抬门禁不抬上限，COMP 桥（>=7U）将保持裁剪且无任何报错——开闸清单必须包含同步抬升该上限。

### 11.5 G00 README 三处

补中断优先级冻结表（新 §6）；"唯一真源 = board_config.h"改为指向目标工程各驱动头；移除与任务清单冲突的 G0-x 节标签（编号说明见该文头部）。

### 11.6 验证与遗留

| 检查 | 结果 |
|---|---|
| 8 项编辑逐字节对照 spec（git diff 亲验） | PASS |
| footprint 恰好 4 文件（g32f031_int.c / drv_comp.h / drv_device.h / drv_comp.c） | PASS |
| #if/#endif 配对（2+2）、ADC 桥在 >=3 块、双 COMP 桥同在 >=7 块 | PASS |
| `INPUT_MINUS_PB1`/COMP2 路径 `DDL_GPIO_PIN_1` 零残留（OPA 的 PA0/1/2 属预期保留） | PASS |
| 断功率调用 4 处（HardFault 1 + 三占位 3） | PASS |
| Keil UV4 命令行 Rebuild（`-b -j0`，退出码 0，`--diag_error=warning` 生效） | **0 Error / 0 Warning** |
| MAP：drv_adc / drv_comp / drv_pwm 三目标文件整体零贡献，向量回落弱桩 0x000010e7 | PASS |
| 镜像总量 Total RO 1,964 B / RW+ZI 1,032 B（Code 1,726 + RO 238 + RW 8 + ZI 1,024） | 远低于 0xEA00 上限 |

Rebuild 执行记录：控制器以 `D:\Keil_v5\UV4\UV4.exe -b IAP_Application.uvprojx -j0 -o rebuild_p1.log` 命令行执行（2026-09-01，日志 `Application/Project/MDK/rebuild_p1.log`，MAP `Listings/G32F031/IAP_Application.map`）。**本节 Rebuild 取代 §4/§10.4 的过期构建证据**，成为当前候选的静态腿基线。

遗留：**实板腿 G1-10**（PA15 全程低 / RUN 灯 1 Hz / RELAY 不吸合）与 **G0 签字/通断**仍未完成——G1 仍为 `IN_PROGRESS`。P2 三项已于 §12 落地，静态腿须再跑一次 Rebuild 才能覆盖本次 diff。

**过程记录**：haiku 执行器报告"8 处全部精确应用"后，sonnet spec 审查发现两处未在报告中的注释删改（MemManage 头注释被截短、drv_comp.h 少插 1 行且丢两项内容），且审查期间文件仍在变。已按字节规格修复并经控制器亲验 diff 与 spec 完全一致。归因未定论（执行器否认且其 old_string 起点确实无法触及被删行）；教训入 `tasks/lessons.md` L10：**子代理报告不可作为交付证据，交接必须控制器亲验 git diff**。

---

## 12. 复审收口（P2 + 文档同步，2026-09-01）

外部复审第二轮之后的代码/文档缺口一次性落地：

| 项 | 落点 |
|---|---|
| 向量桥 include 按门禁裁 | `g32f031_int.c`：`drv_adc.h` 仅 `GATE>=3`，`drv_comp.h` 仅 `GATE>=7` |
| legacy PWM 成对隔离 | `drv_pwm.c` 整文件 `#if GATE>=6`；`bsp_ota.c` 的 `DRV_PWM_Stop` 同步裁剪。G6 必须先改成单端再抬门禁 |
| PinMap 宏编译期检查 | `drv_io.c`：`1<<NUMBER` 对 `GPIO_BSRR_BSn`，不一致 `#error` |
| watchdog 三值入骨架 | `drv_watchdog.h`：TIMEOUT 1000 / GRACE 500 / WINDOW 100 |
| `GATE_KEIL_LINKED` 注释 | 保持 0；写明 §11.6 已有 Rebuild、整门未 PASS |
| COMP0 极性开闸门 | `drv_comp.h` 改为 G7（不再写 G6） |
| docs/46 | §23.2 补断功率；文首 `DRV_DEVICE_GATE_*`；G6/G7 §8.0/§9.0 开闸清单；§9.3 写 PB6/PB2；§23.7 表改为 G1 改代码 / G7 验收 |
| G00 README | BOM/红线宏名改为 `DRV_*`；`app_hw_config.h` 改为已删除 |

**硬前置**：本次改了 `drv_io.c` / `drv_pwm.c` / `bsp_ota.c` / `g32f031_int.c`。Keil Rebuild（2026-09-01，`rebuild_p2.log`）：**0 Error / 0 Warning**，Program Size Code=1726 / RO=238 / RW=8 / ZI=1024（与 §11.6 相同：PinMap 检查是编译期 `#if`，PWM 原本就不在镜像里）。G1 总状态仍为 `IN_PROGRESS`（实板腿未做）。
