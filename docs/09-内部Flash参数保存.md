# 09 · 内部Flash参数保存

## 1. 物理布局与双页轮写

系统无外部EEPROM，利用G32F031K8T内部Flash最后两个512字节物理扇区实现A/B双页Journal：

- Page A：`0x0000FC00`
- Page B：`0x0000FE00`
- 页大小：512字节
- Scatter限制应用代码与数据位于`0x00000000 ~ 0x0000FBFF`，最后1KB专用于非易失存储。

## 2. Journal v3结构与掉电事务

```text
[0..3]   Magic = 0x41555241 ("AURA")
[4..5]   Version = 3
[6..7]   Length = 448
[8..11]  Sequence
[12..15] Payload CRC32
[16..19] Commit Marker = 0xA55AA55A
[20..23] 保留
[24..471] Payload：56字节元数据 + 49×8字节双历史
[472..511] 页尾擦除态/保留
```

提交顺序：

1. 擦除备用页；
2. 写Magic、版本、长度、序号、CRC和除Commit Marker外的内容；
3. 最后单独写Commit Marker；
4. 中途掉电的新页被分类为`INCOMPLETE`，启动时回退到旧有效页；
5. 单页有效、另一页损坏或旧版格式时，在后续安全窗口自愈重写。

## 3. v1/v2兼容迁移

启动时识别：

- v1：基础配置；
- v2：基础配置 + 单套49点PV历史；
- v3：双能量账本、双49点历史、余数、30min相位和Demo设置。

旧格式中的单能量字段有PV输入侧证据，因此迁移到PV实测账本；新增的电池侧估算账本从0建立，禁止把缺乏BAT_I证据的旧数据伪装为实测充电量。

旧Flash若保存了超过v0.10.3 Demo 30W硬上限的配置，应用设置时钳制到30W并标记dirty，下一安全窗口重写；不因单个可修复参数把整页判成不可恢复。

## 4. 双路能量账本

### 4.1 PV实测发电量

来源：

```text
Ppv = Vpv × Ipv
质量 = MEASURED
```

v0.10.3不再只判断缓存中的`pv_power_mw > 0`。必须同时满足：

```text
PV_POWER有效位存在
sequence非0
样本年龄≤AURORA_MEASUREMENT_STALE_MS
无ADC_STALE / ADC_DMA / ADC_OVERRUN
物理PWM实际输出
Ppv>0
```

才累计到`lifetime_energy_wh`和PV余数。ADC停止更新后，最后一帧缓存正功率不会继续增长能量。

### 4.2 电池侧估算充电量

来源：

```text
Pcharge_est = Ppv × η
质量 = ESTIMATED
```

只在以下条件成立时累计：

```text
Battery模式
PowerStage == RUN
Runtime已经写出Relay GPIO
PWM物理输出
Charger允许充电
功率命令非零
PV样本新鲜有效且超过最小传能门槛
```

新板没有BAT_I和Relay机械触点反馈，所以该账本仍是估算，不得描述为电池侧实测能量。

## 5. 24h滚动历史与余数

Flash v3保存：

- PV生命周期与最近24h能量；
- 电池侧估算生命周期与最近24h能量；
- 两套49点累计Wh快照；
- 两类不足1Wh的`mW·ms`余数；
- 当前30min窗口已经经过的时间；
- 电池体系、电压平台、Operating Mode、Demo目标、设置版本和能量语义版本。

49个端点对应48个30min间隔，覆盖最近24h。重启后从已保存的窗口相位继续，而不是把30min起点重置到开机时刻。

## 6. 擦写绝对安全门禁

真正擦写必须满足：

```text
PWM物理关闭
AND Runtime记录的Relay GPIO为OFF
AND dirty保持至少1s
```

运行时每增加整Wh或每60s只提出保存请求，不会为了统计数据强行中断充电或在Relay闭合时擦Flash。

## 7. 突然掉电的真实边界

当前软件没有运行期PVD/LVD掉电保存窗口，也没有FRAM/MRAM。正常Battery运行中Relay可能连续闭合数小时，dirty请求会一直等待安全窗口。因此，突然完全掉电且VDD没有足够保持时间时，可能丢失：

```text
从上一次真正安全写入Flash之后的整段RAM能量增量
```

并不保证只丢最后60s或最后一个不足1Wh余数。

v0.10.3保持正确的安全优先级：

- 不在PWM或Relay活动时擦写；
- NO_SUN、故障退出、模式切换后进入安全窗口时尽快落盘；
- 不用周期性断Relay换取数据保存。

如产品要求严格掉电保存，需要增加掉电保持电容与可验证的PVD/LVD提前量，或采用FRAM/MRAM。该项继续标记为硬件/数据保持限制，不能在纯软件版本中宣称已经关闭。

## 8. 协议语义边界

v0.10.3不改变既有30字节遥测布局。为兼容120W充电量语义，旧daily/lifetime energy字段映射到 `charge_est_*`，质量为ESTIMATED；PV实测账本继续独立保存在Flash v3。
