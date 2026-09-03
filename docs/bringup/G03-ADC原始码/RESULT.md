# G3 ADC 单通道原始码 — 验收记录

- **门禁状态：`IN_PROGRESS`**
- 目标工程：`D:\work\mppt-charger-300w\Application`
- 门禁宏：战役镜像当前为 `DRV_DEVICE_MIGRATION_GATE = 5U`（`driver/inc/drv_device.h`）。G3 软触发路径在源码中以 `#if GATE == 3` 保留；回退到 3 即可单独取证。
- 记录时间：2026-09-02

> 状态判定：G3 开闸四件与 85.8 V 量程前置已写入新 `drv_adc.*`。
> 战役镜像 GATE=5 已 Keil Rebuild **0 Error / 0 Warning**（`rebuild_g5.log`）。
> **实板腿未执行**（逐通道注入、映射、BAT_U 建立时间）。不得记 `PASS`。

---

## 1. 开闸四件（代码腿）

| 项 | 落点 | 状态 |
|---|---|---|
| GPIO 模拟掩码含 `PIN_1`（PB1 / ADC_IN4） | `drv_adc.c` `adc_gpio_analog_6ch()` | 代码已落地 |
| rank / 通道表含 CH4 | `s_adc_rank_ch[]`、`DRV_ADC_CH_BST_U (4U)` | 代码已落地 |
| Sequencer / 索引从 5 改为 6 | `DRV_ADC_CHANNEL_COUNT (6U)` | 代码已落地 |
| 对外命名 **BST_U**；`BUS_U` 仅废弃别名 | `DRV_ADC_INDEX_BST_U`，`BUS_U`=`BST_U` | 代码已落地 |

legacy 5 通道 EOS / `DRV_ADC_Init` / `DRV_ADC_IRQHandler` 已删除，禁止再给旧路径打补丁。

---

## 2. G3 PASS 前置（量程，书面）

- BST_U 分压 26:1，3.3 V 参考下理论满量程 **≈85.8 V**（`DRV_ADC_BST_U_FULL_SCALE_MV = 3300*26`）。
- 注入电压不超过 ADC 合法参考（焊盘 ≤3.0 V / 按分压不超过满量程）。**禁止**为测软件保护把 BST_U 推进超参考区。
- `DRV_ADC_NEAR_FULL_SCALE_CODE (4080)`：近满量程码不可信，不得据此吸 Relay。
- 是否换分压仍挂 doc35 / G5/G9，不在本门关闭。

---

## 3. 代码路径（GATE==3 时）

软触发、1 rank、`TriggerEdge=0`（EXTEN 必须为 0）、不上 DMA。`drv_adc_convert_raw()` 每次改 RANK1 等 EOS。

当前战役 GATE=5，该路径编译裁掉，DMA 路径生效。单独做 G3 台架时把 GATE 改回 3 并 **不要** 开 `drv_adc_start()`。

---

## 4. 实板腿（未做）

- [ ] G3-T01 0.0~3.0 V 逐点 raw/mean/min/max
- [ ] G3-T02 六通道映射（PA8 PV_I … PB1 BST_U … PB5 NTC_AMB）
- [ ] G3-T03 BAT_U 高阻建立时间
- [ ] `meter-data.csv`

---

## 5. 结论

**IN_PROGRESS**：代码开闸完成，无实板 PASS。
