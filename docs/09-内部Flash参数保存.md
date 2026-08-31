# 09 · 内部Flash参数保存

## 1. 物理布局与双页轮写

系统无外部 EEPROM，利用 G32F031K8T 内部 Flash 最后两个 512 字节物理扇区实现 A/B 双页轮写 Journal：

- **Page A 地址**：`0x0000FC00`
- **Page B 地址**：`0x0000FE00`
- **页大小**：512 字节（`BOARD_FLASH_PAGE_SIZE`）
- **Scatter 分区**：代码与数据限制在 `0x00000000 ~ 0x0000FBFF`（63KB），最后 1KB 专用于非易失存储。

## 2. Journal v3 帧结构与防掉电机制

```text
[0..3]   Magic = 0x41555241 ("AURA")
[4..5]   Version = 3 (AURORA_STORAGE_VERSION)
[6..7]   Length = 448 (Payload 字节数)
[8..11]  Sequence (单调递增 uint32)
[12..15] CRC32 (Payload IEEE CRC32)
[16..19] Commit Marker = 0xA55AA55A （两阶段写入核心）
[20..23] 保留
[24..471] Payload (56字节元数据 + 49×8字节双历史)
```

### 两阶段原子提交：
1. 先擦除目标备用页（Sequence 奇偶交替）；
2. 写入 Magic、Version、Length、Sequence、CRC32 及除 Commit Marker 以外的所有 Payload；
3. **最后单独一步写入 Commit Marker（0xA55AA55A）**；
4. 任何写入中途掉电，新页均被判定为 `INCOMPLETE` 丢弃，旧页完好无损；启动时自动回退并排队在安全窗口修复。

## 3. v1/v2 向下兼容与平滑升级

- 启动时自动识别旧版格式：
  - **v1**（16B 基础配置）
  - **v2**（16B 基础 + 49×4B 单路 PV 历史）
  - **v3**（56B 增强配置 + 49×8B 双路历史）
- 读取到 v1/v2 时，自动保留历史 PV 发电量，电池侧估算账本安全从 0 初始化，在安全窗口自愈升级重写为 v3。

## 4. 双路能量统计与 24h 滚动历史

Flash v3 持久化两套独立能量账本：

1. **PV 实测发电量**（`lifetime_energy_wh` + `energy_history_wh[49]`）：实测 $P_{pv} > 0$ 时毫秒积分累计；
2. **电池侧估算充电量**（`charge_est_lifetime_energy_wh` + `charge_est_history_wh[49]`）：仅在真实充电会话按 $P_{pv} \times \eta$ 估算累计（显式保留 ESTIMATED 语义）；
3. **24h 滚动窗口**：49 个快照点，48 个 30 分钟间隔（`AURORA_ENERGY_HISTORY_INTERVAL_MS = 1,800,000ms`）。Flash v3 同步记录 30min 相位进度（`history_interval_elapsed_ms`），重启不丢失窗口时间偏移。

## 5. 擦写绝对安全门禁

为避免 Flash 擦写的高压泵对 MCU 电源轨造成瞬态扰动进而导致 PWM 占空比抖动或死机：

- **擦写条件**：
  $$\text{PWM 物理处于关闭态} \quad \text{AND} \quad \text{继电器物理处于断开态} \quad \text{AND} \quad \text{Dirty 保持} \ge 1\text{s}$$
- **运行期持久化**：
  - 运行中每累积 1Wh 或每 60s（`AURORA_ENERGY_PERSIST_REQUEST_MS`）仅在内存置 `dirty=true`；
  - 真正擦写操作严格推迟至系统待机/关断/故障安全窗口中执行。
