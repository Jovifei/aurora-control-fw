#ifndef AURORA_TEST_SERVICE_COMPAT_H
#define AURORA_TEST_SERVICE_COMPAT_H

/* 仅供v0.8.3 Host回归兼容旧测试符号；生产service层已经删除。 */
#include "main.h"
#include "driver.h"

typedef aurora_runtime_t aurora_service_t;

/*
 * 历史Relay测试手工写app.sample但没有发布Measurement sequence。
 * v0.10.3生产HOLD_OFF由PowerStage保证新序号；这里只为旧Host夹具补齐可识别的快照标识。
 */
static inline void aurora_test_service_poll(aurora_service_t *service)
{
    if ((service != NULL) && (service->app.sample.sequence == 0U) &&
        (service->app.power_command.relay_enable != service->relay_applied))
    {
        service->app.sample.sequence = 1U;
        service->app.sample.timestamp_ms = drv_time_now_ms();
    }
    aurora_runtime_poll(service);
}

#define aurora_service_init                         aurora_runtime_init /* 测试兼容初始化别名。 */
#define aurora_service_poll                         aurora_test_service_poll /* 测试兼容轮询别名。 */
#define aurora_service_isr_tick                     aurora_runtime_isr_tick /* 测试兼容Tick别名。 */
#define aurora_service_isr_adc_block                aurora_runtime_isr_adc_block /* 测试兼容ADC别名。 */
#define aurora_service_isr_fast_fault               aurora_runtime_isr_fast_fault /* 测试兼容故障别名。 */
#define aurora_service_isr_comparator_fault         aurora_runtime_isr_comparator_fault /* 测试兼容CMP别名。 */
#define aurora_service_isr_uart_rx                  aurora_runtime_isr_uart_rx /* 测试兼容UART别名。 */

#endif
