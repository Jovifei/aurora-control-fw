# G5 工程量换算与模拟标定 — 验收记录

- **门禁状态：`IN_PROGRESS`**
- 目标工程：`D:\work\mppt-charger-300w\Application`
- 门禁宏：`DRV_DEVICE_MIGRATION_GATE = 5U`
- 记录时间：2026-09-02

> 状态判定：理论模型 `Physical = K*(code-zero)*polarity/den` 已进入 `drv_board.h/.c`。
> Keil Rebuild **0 Error / 0 Warning**（`rebuild_g5.log`）。
> **`DRV_DEVICE_GATE_ANALOG_CALIBRATED` 保持 0**（未拟合）。
> **`DRV_DEVICE_POWER_OUTPUT_ALLOWED` 保持 0**。
> 实板标定点未做。不得记 `PASS`。G5 不是功率放行门。

---

## 1. 理论系数（未拟合）

| 通道 | 模型 | 备注 |
|---|---|---|
| PV_U | 26:1 → ≈20.95 mV/code | |
| BAT_U | 15510/510 ≈30.41 → ≈24.51 mV/code | 换算走 int64，避免 32 位溢出 |
| BST_U | 26:1，满量程 ≈85.8 V | 87~93 V **不可**标称可测；4080 打 `!SAT` |
| PV_I | 16.79 mA/code，zero=2048，极性 +1 | **G7 之前 OPA 未 Init，本值不能当产品电流** |
| NTC | 5.1k / 100k；`R=raw×5100/(4096-raw)` + 120W `TabNtc_100K` 166 点（与 aurora `measurement.c` 同表） | 开路哨兵 32767，短路 -32768；**不是**恒定 β 折线 |

拟合后只允许回填 `drv_board.h`，禁止写入 `main.h` / 任何 `app/` 头。

---

## 2. BST_U 量程风险

软件系数无法恢复已经饱和的信息。近满量程码必须当不可信。硬件是否换分压挂 doc35 / G9，本门只落档风险，不改分压。

注入 BST_U 台架点建议 0/10/20/40/60/75/82 V，不要故意超参考。

---

## 3. 实板腿（未做）

- [ ] 电压/电流标定点与拟合报告
- [ ] NTC 短路/开路/0/25/60/95/105℃
- [ ] 精度门：电压 ≤1% reading；PV_I(≥2A) ≤3%；NTC ≤2℃
- [ ] `GATE_ANALOG_CALIBRATED` 签字后才允许置 1

---

## 4. 结论

**IN_PROGRESS**：理论换算已编译进 GATE=5 镜像；无标定签字，功率总门保持 0。
