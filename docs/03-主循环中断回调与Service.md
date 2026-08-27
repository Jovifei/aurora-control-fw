# 03 · 主循环、中断回调与Service

## 1. 中断只搬运和关断

```text
SysTick ISR       → EVENT_TICK
DMA ISR           → 完成块索引/错误 → EVENT_ADC或FAST_FAULT
COMP/Break ISR    → 硬关PWM → fault mask → EVENT_FAST_FAULT
ATMR Update ISR   → 仅首次arm时确认0占空比已由自然UEV装载
USART ISR         → 最多搬运32字节 → EVENT_UART_RX
                         ↓
                    aurora_service_poll()
                         ↓
                    APP算法与状态机
```

永久运行的50 kHz UPDATE中断已经删除。UPDATE中断只在首次发波前写入0占空比后临时开启一次，确认自然UEV装载完成即关闭。

## 2. 主循环服务预算

- UART硬件ISR每次最多搬运32字节；
- UART主循环每轮最多消费64字节，剩余数据重新投递事件；
- ADC主循环只处理已经完成的半缓冲；同一半缓冲绕回时锁存`ADC_OVERRUN`；
- Flash只在PWM关闭且继电器断开时执行；
- 每次真正arm或写Duty前后都重新检查故障、Break源、Break锁存、epoch和板级门禁。

## 3. 中断优先级

```text
0  COMP0 / COMP2 / ATMR Break
1  ADC DMA
2  SysTick
3  USART
```

快速故障必须能抢占ADC和通信。ISR中禁止MPPT、充电状态机、温度查表、Flash写入、完整协议解析、日志格式化和无界等待。
