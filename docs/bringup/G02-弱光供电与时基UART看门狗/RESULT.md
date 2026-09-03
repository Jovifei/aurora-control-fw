# G2 弱光供电 / 时钟 / 1 ms 节拍 / UART / 看门狗 — 验收记录

- **门禁状态：`IN_PROGRESS`**
- 目标工程：`D:\work\mppt-charger-300w\Application`
- 门禁宏：`DRV_DEVICE_MIGRATION_GATE = 2U`（`driver/inc/drv_device.h`）
- 记录时间：2026-09-01

> 状态判定：代码腿 A→B→C→D 已落地；Keil Rebuild **0 Error / 0 Warning**（`rebuild_g2.log`）。
> **实板腿**（PVD 升降沿、PB11 1 kHz 抖动、UART 10^6 回环、IWDT 注入、2 h 无误复位）**未执行**。
> **静态 MAP 门**：`DRV_PWM_Init` / `DRV_ADC_Init` 已被链接器 GC，无 MOE 置位路径引用。

---

## 1. 门禁定义与推荐顺序

严格 **A → B → C → D**（看门狗最后，便于 UART + 时基区分真 WDG 复位）：

| 阶段 | 内容 | 代码落点 |
|---|---|---|
| G2-A | PVD 供电资格等待（2.8 V / 100 ms 候选） | `drv_system_wait_for_supply_stable()` |
| G2-B | HSI 64 MHz + 1 ms SysTick；**仅 GATE==2** 时 PB11 翻转 | `drv_time_tick_isr()` |
| G2-C | Debug UART PB7/PB8 + BOOT 日志 + RX 回环 | `drv_debug_uart.*` |
| G2-D | IWDT（1000/500/100 ms）+ 主循环 feed | `drv_watchdog.*` |

本阶段**不 Init**：ADC / PWM / OPA / COMP / 产品 USART / OTA。

---

## 2. 代码改动清单

### 2.1 新建

| 文件 | 作用 |
|---|---|
| `driver/src/drv_debug_uart.c` | UART@PB7/PB8 AF0、TX/RX 环、BOOT 行、回环 poll |
| `driver/src/drv_watchdog.c` | IWDT Init（含 APB 时钟）、票据窗 feed |

### 2.2 修改

| 文件 | 改动 |
|---|---|
| `driver/inc/drv_device.h` | `MIGRATION_GATE` 1→**2**；G0–G5 边界 `#error` |
| `driver/src/drv_device.c` | 复位策略 → 时钟/GPIO/节拍 → **A PVD → C UART/BOOT → D IWDT** |
| `driver/src/drv_system.c` | `reset_policy_apply`、`capture_reset_flags`、PB11 探针、`g_pvd_stable_ms` |
| `driver/inc/drv_system.h` | `drv_system_reset_flags_t`、新 API |
| `driver/inc/drv_watchdog.h` | API + 票据宏 |
| `driver/inc/drv_debug_uart.h` | API + RX 缓冲 |
| `app/src/main.c` | 主循环：看门狗 service + UART poll（G1 RUN 灯保留在 `#else`） |
| `app/src/g32f031_int.c` | `UART_IRQHandler` 桥（GATE≥2） |
| `driver/src/drv_io.c` | PVD 等待前 PB7 拉高，避免 UART Init 前噪声 |
| `Project/MDK/IAP_Application.uvprojx` | +`drv_debug_uart.c` +`drv_watchdog.c` +`g32f031_ddl_uart.c` +`debug.c` |

### 2.3 上电调用链（gate = 2）

```text
main()
 └─ drv_device_config()
     ├─ drv_system_reset_policy_apply()     PVDRSTEN=0, LOCKUPRSTEN=0
     ├─ drv_system_capture_reset_flags()
     ├─ drv_sysclk_config()                 64 MHz
     ├─ drv_io_init()                       功率脚安全低；PB7 空闲高
     ├─ drv_system_init()                   1 ms SysTick
     ├─ drv_irq_configure_priorities()
     ├─ drv_system_wait_for_supply_stable() G2-A
     └─ drv_debug_uart_init()               G2-C 传输层
 └─ debug_boot_logo_print()
 └─ debug_boot_log_print()                  BOOT/FW/GIT_SHA/RESET/CLOCK/PVD/GATE
 └─ drv_system_clear_reset_flags()          RCC Unlock 后清八源
 └─ drv_device_start_watchdog()             G2-D（最后）
 └─ while(1)
     ├─ drv_watchdog_on_main_loop()
     ├─ drv_watchdog_service()
     └─ drv_debug_uart_poll()               RX→TX 回环
```

---

## 3. 静态编译证据

| 项 | 结果 | 证据 |
|---|---|---|
| Keil Rebuild | ✅ 0e/0w | `Application/Project/MDK/rebuild_g2_review.log` |
| Program Size | Code=8732 RO=1200 RW=56 ZI=1792 | 同上 |
| MAP：PWM Init | ✅ 未链入 | `Removing drv_pwm.o(... DRV_PWM_Init ...)`（gate≥6 整文件隔离 + GC） |
| MAP：ADC Init | ✅ 未链入 | `Removing drv_adc.o(i.DRV_ADC_Init)` |
| MAP：ATMR MOE | ✅ 无应用引用 | 无 `DRV_PWM_*` 符号链入 |

---

## 4. 启动日志格式（G2-C）

单行示例（字段顺序固定）：

```text
BOOT FW=V0.2.0 GIT_SHA=unknown RESET=OPTRST=0,... CLOCK_HZ=64000000 PVD_STABLE_MS=100 GATE=2
[GE_DEBUG][PVD] supply stable 100 ms
[GE_DEBUG][WDG] started timeout=1000 grace=500 window=100
```

- Geeco Logo 由 `debug_boot_logo_print()` 在 BOOT 行之前输出。
- `RESET=` 八源读 `RCC->RSTCSR`；日志后 `drv_system_clear_reset_flags()` 清除八源。
- 模块开关：`app/inc/main.h`；打印宏：`app/inc/debug.h`（参照 smart-controller 模式）。
- TX 环满：**丢整段**并递增 `drv_debug_uart_get_tx_drop_count()`。

---

## 5. 看门狗不变量（G2-D）

| 参数 | 值 |
|---|---|
| TIMEOUT | 1000 ms |
| STARTUP_GRACE | 500 ms |
| WINDOW | 100 ms |
| 票据 | MAIN（主循环）+ CONTROL（1 ms ISR） |
| ISR | **只置票，禁止 feed** |
| IWDT 时钟 | `DDL_APB_GRP1_EnableClock(IWDT)` 已补 |

---

## 6. 实板验收项（待执行）

| ID | 项 | 期望 | 状态 |
|---|---|---|---|
| G2-A-01 | PVD 快速上升 | 满 100 ms 窗口后放行一次 | ⚠️ |
| G2-A-02 | PVD 慢坡 / 阈值抖动 | 掉落清零重等，不 Fault | ⚠️ |
| G2-A-03 | 上升沿 ~2.8 V / 下降沿 ~2.7 V | 分点实测并记录容差 | ⚠️ |
| G2-B-01 | PB11 示波器 | 1 kHz、无丢拍、空载抖动 <10 µs | ⚠️ |
| G2-C-01 | BOOT 行 | 115200 8N1 可读 | ⚠️ |
| G2-C-02 | 10^6 B 回环 | 无错误；RX 满有计数 | ⚠️ |
| G2-D-01 | 停主循环喂狗 | ≤~1 s IWDT 复位，`IWDTRST=1` | ⚠️ |
| G2-D-02 | 复位 GLC/Relay | 无毛刺 | ⚠️ |
| G2-D-03 | 2 h 运行 | 无误复位 | ⚠️ |

---

## 7. 未完成 / 阻塞

- G1 实板三项未 PASS → 按路线应在 G1 PASS 后再记 G2 PASS；当前代码已抬门至 2 供联调。
- PVD 2.8 V / 100 ms 仍为**候选**，台架实测后回填 `drv_system.h`。

### 7.1 审计项（代码已修，实板未验）

| 项 | 结论 | 落点 |
|---|---|---|
| G2-C4 RSTCSR 清除 | ✅ Unlock 后八源 `DDL_RCC_ClearFlag_*` | `drv_system_clear_reset_flags()` |
| G2-C4 GIT_SHA | ✅ BOOT 行含 `GIT_SHA=`（默认 unknown） | `main.h` |
| 分层：Driver 不含 app | ✅ BOOT/Logo 在 `app/debug.c` | `drv_debug_uart` 仅传输层 |
| CONTROL 票 GATE>=2 | ✅ 探针仍 `==2`；置票 `>=2` | `drv_time_tick_isr()` |
| POWER_OUTPUT #error | ✅ 守卫在宏定义之后 | `drv_device.h` |
| G6/G7 开闸警示 | ✅ | `drv_device.c` 尾部 |

---

## 8. 已知行为（审计记入）

| 项 | 说明 |
|---|---|
| HardFault × IWDT | G2 起 IWDT 常开；HardFault 自旋约 1 s 后 IWDT 复位，无法长时间停在原地等调试器。LOCKUPRSTEN=0 仍保留 GLC 拉低窗口，但 halt 期间 IWDT 不冻结。 |
| 首喂时刻 | 约 GRACE+WINDOW=600 ms，须小于 TIMEOUT=1000 ms；`drv_watchdog.h` 有编译期 `#error` |
| PB7 弱光等待 | `drv_io_init` 在 PVD 等待前即将 PB7 拉高，避免 UART Init 前噪声。 |

---

## 9. 编译证据（审计修复后）

| 项 | 值 |
|---|---|
| 日志 | `Application/Project/MDK/rebuild_g2_review.log` |
| 结果 | 0 Error / 0 Warning |
| Code / RO / RW / ZI | 8732 / 1200 / 56 / 1792 |

---

## 10. 目标仓文档指针

详细模块说明见：`D:\work\mppt-charger-300w\docs\04-LOG-记录\08-LOG-G2弱光供电与时基UART看门狗-代码更新记录.md`

---

## 11. 二次审计修复（2026-09-01，Jovi 指令"直接修改 bug"）

> 触发：二次审计（本文件 §8 前的复核）发现重构 debug 层引入两个运行期 bug——构建 0/0 测不出。

### 11.1 Bug 1：Logo 挤爆 TX 环，BOOT/PVD/WDG 行全部静默丢弃

**机理（确定性推算，非实测）**：Logo 共 11 行 × ~93 B ≈ 1 KB，经 `debug_puts_raw` **逐字节**灌 256 B TX 环；主循环压入速率（µs/字节）>> 排泄速率（115200 波特 ≈ 11.5 B/ms），环在 ~255 字节处填满（第 3 行中部）→ `write` 拒收 → `break` → **Logo 截断在 2.7 行**；紧随其后的 BOOT 行（~190 B）、PVD 行、WDG 行在环满状态下整段丢弃。净效果：**G2-C4 的 BOOT 行在实板上一行都出不来**，且每次上电 `tx_drop_count` ≥3，污染 G2-C6 计数基线。

**修复**：
- 驱动新增唯一原语 `drv_debug_uart_flush(timeout_ms)`（阻塞等 TX 环排空，SysTick 时基，超时不丢数据）；
- `debug_puts_raw` 改"每行先排空再整行 write"（单行 93 B < 环容量 255，行内完整性由环空保证）；
- `debug_boot_log_print` 与 `debug_printf_locked` 写入前 flush——BOOT/PVD/WDG 行逐条完整到达。
- 启动期总阻塞 ≈ 118 ms（Logo 逐行 ~90 ms + BOOT 排空 ~8 ms + PVD ~17 ms + WDG ~3 ms），全部发生在 IWDT 启动前或宽限期内，安全。

### 11.2 Bug 2：`debug_printf_locked` 假锁 + 无效计数调用

- 函数名承诺锁但体内无任何临界区 → 已加真锁：`drv_irq_save/restore` 包住 vsnprintf（共享静态缓冲 `s_debug_printf_buf` 的互斥；持锁时间即一次格式化，SysTick 延迟增量可忽略）；
- `(void)drv_debug_uart_get_tx_drop_count();` 空语句（原 TODO 已由 Jovi 裁决"补 APP 侧计数器"）→ 新增 `s_debug_printf_drop_count`，三种丢弃路径（vsnprintf 失败 / 行超长 / 环拒收）全部计数，`debug_printf_get_drop_count()` 供台架判据，与驱动侧 `tx_drop_count` 分层统计。

### 11.3 顺带（ponytail，同文件死代码）

删除零调用公共 API `drv_debug_uart_write_str`（30 行；其"自动补 CRLF"语义与 Logo 自带 \r\n 的字符串冲突，本就不是正确工具）；`debug.h` 移除无用的 `#include <stdio.h>`。

### 11.4 验证（2026-09-01，`rebuild_g2_bugfix.log`）

| 检查 | 结果 |
|---|---|
| Keil UV4 命令行 Rebuild | **0 Error / 0 Warning** |
| `write_str` 全工程零残留 / `flush` 驱动定义+声明+app 3 调用点 | PASS |
| `drv_irq_save/restore` 配对（debug.c:54/58） | PASS |
| 无效 getter 调用清零、丢弃计数三路径齐 | PASS |
| 分层单向（driver 零 app 头） | PASS |
| 块注释状态机（4 文件无嵌套/未闭合） | PASS |
| MAP：`drv_debug_uart_flush`(34 B) 在位、`write_str` 缺席、功率/采集模块仍零贡献（G2-E3 未回归） | PASS |
| 镜像 Total RO 9,984 B / RW+ZI 1,848 B（净 +56 B） | 远低于上限 |

> **注意**：本节修复后 BOOT 日志链路的正确性仍是**推算 + 构建级验证**，串口实际输出待 G2-C-01 台架项确认。
