#ifndef AURORA_MAIN_H
#define AURORA_MAIN_H

#include "app_config.h"
#include "charger.h"
#include "measurement.h"
#include "mppt.h"
#include "power_stage.h"
#include "protection.h"
#include "protocol.h"
#include "storage.h"
#include "ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 应用运行时UART接收环形缓冲长度；这是产品调度资源，不属于MCU寄存器配置。 */
#define AURORA_RUNTIME_UART_RX_BUFFER_SIZE          (256U)
/* 主循环单次最多消费的UART字节数，避免通信饿死控制任务。 */
#define AURORA_RUNTIME_UART_RX_BUDGET               (64U)
/* USART ISR单次最多搬运的RX字节数，保证中断有界。 */
#define AURORA_RUNTIME_UART_ISR_RX_BUDGET           (32U)

/* 纯业务组合根：不保存寄存器对象，只保存测量、充电、保护、MPPT等产品状态。 */
typedef struct
{
    aurora_mppt_ctx_t mppt;                          /* PV参考电压搜索与PI状态。 */
    aurora_charger_ctx_t charger;                    /* 电池档案与TC/CC/CV/Float状态机。 */
    aurora_power_stage_ctx_t power_stage;            /* 启动、预充、继电器和Duty执行器。 */
    uint64_t energy_accumulator_mw_ms;               /* 尚未折算为整Wh的能量余数。 */
    aurora_measurement_ctx_t measurement;            /* ADC数据处理、NTC与运行时零点。 */
    aurora_protection_ctx_t protection;              /* 毫秒级保护、锁存和恢复状态。 */
    aurora_storage_ctx_t storage;                    /* 双页Flash Journal状态。 */
    aurora_protocol_ctx_t protocol;                  /* 产品UART协议解析状态。 */
    aurora_measurement_t sample;                     /* 最近一次可供控制使用的测量快照。 */
    uint32_t last_step_ms;                           /* 上一次1ms应用调度时间。 */
    uint32_t last_10ms;                              /* 上一次10ms控制链运行时间。 */
    uint32_t telemetry_message_id;                   /* 主动遥测消息序号。 */
    aurora_mppt_output_t mppt_output;                /* 最近一次MPPT输出。 */
    aurora_charge_output_t charge_output;            /* 最近一次充电目标与PV包络。 */
    aurora_power_command_t power_command;            /* 待运行层落实的功率命令。 */
    aurora_ui_ctx_t ui;                              /* LED相位状态。 */
    aurora_ui_output_t ui_output;                    /* 待运行层落实的LED命令。 */
    bool link_request;                               /* V2.7 Link逻辑请求。 */
    uint8_t layout_reserved[7];                      /* 显式补齐应用组合根。 */
} aurora_app_t;

/*
 * 两层架构中的应用运行根。
 * 原service层的事件邮箱、PWM授权、看门狗健康票据和通信缓冲全部并入app/main.c，
 * 但真正的寄存器/DDL访问仍只允许发生在driver/src。
 */
typedef struct
{
    aurora_app_t app;                                /* 产品业务组合根。 */
    volatile uint32_t safety_epoch;                  /* 快速故障递增，废弃旧PWM授权。 */
    volatile uint32_t pending_fault_mask;            /* ISR待主循环锁存的故障位。 */
    volatile uint32_t event_flags;                   /* ISR发布、主循环领取的事件位。 */
    volatile uint32_t adc_timestamp_ms[2];           /* DMA双半块完成时间。 */
    uint32_t pwm_zero_sequence;                      /* 首次零CCR等待确认的提交序号。 */
    uint32_t watchdog_seen;                          /* 当前健康窗口已收到票据。 */
    uint32_t watchdog_window_start_ms;               /* 看门狗健康窗口起点。 */
    uint32_t watchdog_started_ms;                    /* IWDT启动时间。 */
    uint32_t last_telemetry_ms;                      /* 上次主动遥测时间。 */
    uint32_t fast_ocp_recover_since_ms;              /* 快速OCP硬件源消失后的恢复计时。 */
    volatile uint32_t adc_overrun_count;             /* ADC半缓冲覆盖计数。 */
    volatile uint32_t uart_rx_overrun_count;         /* RX环形缓冲溢出计数。 */
    volatile uint32_t startup_comp_ignored_count;    /* PWM未输出阶段CMP诊断次数。 */
    volatile uint16_t uart_head;                     /* RX写索引。 */
    volatile uint16_t uart_tail;                     /* RX读索引。 */
    volatile uint8_t adc_completed_mask;             /* ISR已发布的DMA半块。 */
    volatile uint8_t adc_processing_mask;            /* 主循环正在读取的DMA半块。 */
    uint8_t pwm_arm_state;                           /* PWM零CCR/放行握手状态。 */
    uint8_t uart_rx[AURORA_RUNTIME_UART_RX_BUFFER_SIZE]; /* ISR写、主循环读的RX缓冲。 */
    bool relay_applied;                              /* 物理继电器最近实际状态。 */
    bool initialized;                                /* 完整运行初始化完成。 */
    uint8_t layout_reserved[2];                      /* 显式补齐。 */
} aurora_runtime_t;

/* 目标中断桥接使用的唯一应用运行实例。 */
extern aurora_runtime_t g_aurora_runtime;

/* 业务接口：由main.c内部运行层和Host测试调用。 */
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
void aurora_app_step_1ms(aurora_app_t *app,
                         uint32_t now_ms,
                         bool boost_output_active);
void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms);

/* 运行接口：替代旧service层；负责APP调度和Driver调用，不允许直接访问寄存器。 */
bool aurora_runtime_init(aurora_runtime_t *runtime);
void aurora_runtime_poll(aurora_runtime_t *runtime);
void aurora_runtime_isr_tick(aurora_runtime_t *runtime);
void aurora_runtime_isr_adc_block(aurora_runtime_t *runtime, uint8_t block_index);
void aurora_runtime_isr_fast_fault(aurora_runtime_t *runtime, uint32_t fault_mask);
void aurora_runtime_isr_comparator_fault(aurora_runtime_t *runtime, uint32_t fault_mask);
void aurora_runtime_isr_uart_rx(aurora_runtime_t *runtime, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif
