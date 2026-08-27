#ifndef AURORA_SERVICE_H
#define AURORA_SERVICE_H

#include "app.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* APP与目标驱动之间的唯一组合和事件桥接上下文。 */
typedef struct
{
    aurora_app_t app;                                /* 纯业务应用组合根。 */
    volatile uint32_t safety_epoch;                  /* 每次快速故障递增，废弃旧授权。 */
    volatile uint32_t pending_fault_mask;            /* ISR待主循环锁存的故障位。 */
    volatile uint32_t event_flags;                   /* ISR发布、主循环领取的事件位。 */
    volatile uint32_t adc_timestamp_ms[2];           /* 两个DMA半缓冲的完成时间。 */
    uint32_t pwm_zero_sequence;                      /* 首次零CCR等待确认的提交序号。 */
    uint32_t watchdog_seen;                          /* 本健康窗口已收到的票据。 */
    uint32_t watchdog_window_start_ms;               /* 当前健康窗口起点。 */
    uint32_t watchdog_started_ms;                    /* IWDT启动时间。 */
    uint32_t last_telemetry_ms;                      /* 上次主动遥测时间。 */
    volatile uint32_t adc_overrun_count;             /* ADC半缓冲覆盖计数。 */
    volatile uint32_t uart_rx_overrun_count;         /* RX环形缓冲溢出计数。 */
    volatile uint8_t adc_completed_mask;             /* ISR已发布的DMA半块。 */
    volatile uint8_t adc_processing_mask;            /* 主循环正在读取的DMA半块。 */
    uint8_t pwm_arm_state;                           /* PWM零CCR/放行握手状态。 */
    uint8_t uart_rx[BOARD_UART_RX_BUFFER_SIZE];      /* ISR写、主循环读的RX环形缓冲。 */
    volatile uint16_t uart_head;                     /* RX写索引。 */
    volatile uint16_t uart_tail;                     /* RX读索引。 */
    bool initialized;                                /* 全部关键模块初始化完成。 */
} aurora_service_t;

bool aurora_service_init(aurora_service_t *service);
void aurora_service_poll(aurora_service_t *service);

/* ISR只搬运数据或发布事件，复杂业务全部在aurora_service_poll中运行。 */
void aurora_service_isr_tick(aurora_service_t *service);
void aurora_service_isr_adc_block(aurora_service_t *service, uint8_t block_index);
void aurora_service_isr_fast_fault(aurora_service_t *service, uint32_t fault_mask);
void aurora_service_isr_pwm_update(aurora_service_t *service);
void aurora_service_isr_uart_rx(aurora_service_t *service, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif
