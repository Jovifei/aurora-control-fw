#ifndef AURORA_MAIN_H
#define AURORA_MAIN_H

#include "charger.h"
#include "measurement.h"
#include "mppt.h"
#include "power_stage.h"
#include "protection.h"
#include "protocol.h"
#include "storage.h"
#include "ui.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 应用层组合根：保存业务状态，不直接保存寄存器或具体驱动对象。 */
typedef struct
{
    aurora_mppt_ctx_t mppt;                          /* PV参考电压搜索与PI状态。 */
    aurora_charger_ctx_t charger;                    /* 电池档案与TC/CC/CV/Float状态机。 */
    aurora_power_stage_ctx_t power_stage;            /* 预充、继电器和Duty执行器。 */
    uint64_t energy_accumulator_mw_ms;               /* 尚未折算为整Wh的能量余数。 */
    aurora_measurement_ctx_t measurement;            /* ADC数据处理与原子快照。 */
    aurora_protection_ctx_t protection;               /* 软件故障去抖、锁存和恢复状态。 */
    aurora_storage_ctx_t storage;                    /* 双页Flash Journal状态。 */
    aurora_protocol_ctx_t protocol;                  /* 产品UART协议解析状态。 */
    aurora_measurement_t sample;                     /* 最近一次可供控制使用的测量快照。 */
    uint32_t last_step_ms;                           /* 上一次1ms应用调度时间。 */
    uint32_t last_10ms;                              /* 上一次10ms控制链运行时间。 */
    uint32_t telemetry_message_id;                   /* 主动遥测消息序号。 */
    aurora_mppt_output_t mppt_output;                /* 最近一次MPPT输出。 */
    aurora_charge_output_t charge_output;            /* 最近一次充电状态机输出。 */
    aurora_power_command_t power_command;            /* 待应用主循环落实的功率命令。 */
    aurora_ui_ctx_t ui;                              /* LED相位状态。 */
    aurora_ui_output_t ui_output;                    /* 待应用主循环落实的LED命令。 */
    uint8_t layout_reserved[10];                     /* 显式补齐应用组合根，避免64位对齐产生隐式填充。 */
} aurora_app_t;

/* 应用运行时上下文：把ISR事件邮箱和驱动落实状态放在应用层组合根。 */
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
    volatile uint16_t uart_head;                     /* RX写索引。 */
    volatile uint16_t uart_tail;                     /* RX读索引。 */
    volatile uint8_t adc_completed_mask;             /* ISR已发布的DMA半块。 */
    volatile uint8_t adc_processing_mask;            /* 主循环正在读取的DMA半块。 */
    uint8_t pwm_arm_state;                           /* PWM零CCR/放行握手状态。 */
    uint8_t uart_rx[BOARD_UART_RX_BUFFER_SIZE];      /* ISR写、主循环读的RX环形缓冲。 */
    bool initialized;                                /* 全部关键模块初始化完成。 */
} aurora_app_runtime_t;

/* 目标中断向量访问的应用运行时上下文。 */
extern aurora_app_runtime_t g_aurora_app_runtime;

void aurora_app_init(aurora_app_t *app,
                     const aurora_measurement_calibration_t *calibration,
                     uint32_t now_ms);
void aurora_app_apply_settings(aurora_app_t *app,
                               const aurora_persistent_settings_t *settings,
                               uint32_t now_ms);
void aurora_app_on_adc_block(aurora_app_t *app,
                             const uint16_t *raw,
                             size_t word_count,
                             uint32_t timestamp_ms);
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms);
void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms);
void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms);

bool aurora_app_runtime_init(aurora_app_runtime_t *runtime);
void aurora_app_runtime_poll(aurora_app_runtime_t *runtime);

/* ISR只搬运数据或发布事件，复杂业务全部在aurora_app_runtime_poll中运行。 */
void aurora_app_runtime_isr_tick(aurora_app_runtime_t *runtime);
void aurora_app_runtime_isr_adc_block(aurora_app_runtime_t *runtime,
                                      uint8_t block_index);
void aurora_app_runtime_isr_fast_fault(aurora_app_runtime_t *runtime,
                                       uint32_t fault_mask);
void aurora_app_runtime_isr_pwm_update(aurora_app_runtime_t *runtime);
void aurora_app_runtime_isr_uart_rx(aurora_app_runtime_t *runtime, uint8_t byte);

/* 目标启动入口；Host构建由测试入口提供。 */
int main(void);

#ifdef __cplusplus
}
#endif

#endif
