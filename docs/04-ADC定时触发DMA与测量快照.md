# 04 · ADC定时触发、DMA与测量快照

## 1. 硬件链路

GTMR按官方Application以10 kHz（100 µs）产生TRGO上升沿；ADC规则组以该触发做一次六通道扫描。采样时间统一为64 ADC时钟周期。产品仍用DMA双半缓冲，不改用官方五通道EOS轮询：

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

ADC时钟按64 MHz/4配置。触发改为与PWM解耦的10 kHz GTMR后，六通道均可使用官方64采样周期；完整序列远小于100 µs触发间隔。DMA半块仍为16次扫描，约1.6 ms发布一次。

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
