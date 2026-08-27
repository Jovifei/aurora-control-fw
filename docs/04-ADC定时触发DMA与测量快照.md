# 04 · ADC定时触发、DMA与测量快照

## 1. 硬件链路

ATMR CH3在每个20 µs PWM周期的中点产生内部OC3REF下降沿；ADC规则组以该下降沿作为外部触发，一次扫描六路：

```text
Rank1 PV_I
Rank2 PV_U
Rank3 BAT_U
Rank4 BST_U
Rank5 NTC_MOS
Rank6 NTC_AMB
```

`drv_adc_start()`不仅使能DMA，还调用`DDL_ADC_StartConversion()`，用于武装规则组等待外部触发；缺少这一步时，定时器有TRGO也不会开始转换。

## 2. 转换预算

ADC时钟按64 MHz/4配置。前四路使用8采样周期，两路NTC使用16采样周期，目的是让完整六通道序列在下一个20 µs触发前结束。旧的32/64采样周期配置会逼近或超过PWM周期，已禁止恢复。

该预算仍必须在实板上用以下证据确认：

- ADC OVR/DMA TE始终为0；
- DMA半满/全满间隔稳定；
- PV_I采样点避开GLC边沿和振铃；
- 高源阻分压节点在当前采样时间下无明显建立误差。

## 3. DMA双块所有权

```text
ADC → DMA循环缓冲[block0 | block1]
          ↓ 半满/全满ISR
只发布块号、序号和时间戳
          ↓
主循环标记processing
          ↓
measurement对完整块去极值平均和换算
          ↓
发布只读物理量快照
```

DMA ISR是唯一生产者，主循环是唯一消费者。若DMA在主循环处理期间再次写到同一半块，系统不静默接受，而是锁存`AURORA_FAULT_ADC_OVERRUN`。

MPPT、充电、保护、通信只读取快照；驱动接口没有“应用主动触发一次ADC”或“读单通道”的入口。
