# G4 ADC 定时触发 + 六通道 DMA — 验收记录

- **门禁状态：`IN_PROGRESS`**
- 目标工程：`D:\work\mppt-charger-300w\Application`
- 门禁宏：`DRV_DEVICE_MIGRATION_GATE = 5U`（G4 路径为 `#if GATE >= 4`）
- 记录时间：2026-09-02

> 状态判定：GTMR 10 kHz TRGO + 6 rank + DMA 双半缓冲 + ADC 健康票已写入。
> **现工程历史上的五通道 EOS 不算 G4。**
> Keil Rebuild **0 Error / 0 Warning**（`rebuild_g5.log`，2026-09-02）。listing 含 `g32f031_ddl_dma.o` / `DMA_CH1_IRQHandler` → `drv_adc_dma_irq_ack`；无 `DRV_ADC_Init`、无 `EnableAllOutputs`。
> 实板 2 h / stale / overrun 未闭环。不得记 `PASS`。

---

## 1. 代码落点

| 项 | 落点 |
|---|---|
| `g32f031_ddl_dma.c` 加入 uvprojx | Group `G32F031_DDL_Driver` |
| 6 rank CH1..CH6，索引 0=PV_I … 3=BST_U … 5=NTC_AMB | `drv_adc.c` `adc_init_dma()` |
| DMA CH1 循环，`g_adc_dma[2][96]` = 384 B | 双半区 HT=块0 / TC=块1 |
| ISR 只认块、记序号、置 ADC 票、清标志 | `drv_adc_dma_irq_ack()`；`g32f031_int.c` `DMA_CH1_IRQHandler` |
| producer/consumer/overwrite/overrun | `drv_adc_health_t` + `drv_adc_take_snapshot()` |
| 看门狗 ADC 票 `1UL<<1` | ISR 只置票；`service()` 在 GATE>=4 要求 MAIN\|CONTROL\|ADC |

G6 才写 ATMR。目标 `ATMR_CR1_UDISEN` 是 bit2，禁止照抄 aurora `drv_pwm.c`。

---

## 2. ISR 不变量

允许：确认完成块、sequence++、置 ADC 票、清 HT/TC/TE。

禁止：温度查表、MPPT、printf、Flash、`drv_watchdog_feed()`。

纯 TE 不置 ADC 票，避免 DMA 挂死仍能过 100 ms 窗。

---

## 3. 实板 / 静态腿（未做）

- [ ] 六通道 DMA 顺序注入确认
- [ ] stale / overrun 可检出
- [ ] 连续 2 h 无 DMA 异常、无误复位
- [ ] Keil MAP：`g32f031_ddl_dma.o` 已链接；无 `DRV_ADC_*` 旧符号；无 ATMR MOE 置位路径
- [ ] `counters.csv` / 示波器

---

## 4. 结论

**IN_PROGRESS**：代码链已落地，无台架 PASS，无 MAP 签字。
