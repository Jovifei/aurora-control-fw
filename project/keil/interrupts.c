#include "service.h"

#include "app_types.h"
#include "board_config.h"
#include "driver.h"
#include "g32f031_ddl_atmr.h"

extern aurora_service_t g_aurora_service;

void SysTick_Handler(void);
void DMA_CH1_IRQHandler(void);
void COMP0_IRQHandler(void);
void COMP1_2_3_IRQHandler(void);
void ATMR_BRK_UP_TRG_COM_IRQHandler(void);
void USART_IRQHandler(void);
void HardFault_Handler(void);

void SysTick_Handler(void)
{
    aurora_service_isr_tick(&g_aurora_service);
}

void DMA_CH1_IRQHandler(void)
{
    const uint8_t completed = drv_adc_dma_irq_ack();
    if ((completed & DRV_ADC_IRQ_ERROR) != 0U)
    {
        aurora_service_isr_fast_fault(&g_aurora_service, AURORA_FAULT_ADC_DMA);
    }
    if ((completed & DRV_ADC_IRQ_BLOCK0) != 0U)
    {
        aurora_service_isr_adc_block(&g_aurora_service, 0U);
    }
    if ((completed & DRV_ADC_IRQ_BLOCK1) != 0U)
    {
        aurora_service_isr_adc_block(&g_aurora_service, 1U);
    }
}

static void handle_fast_comparator_fault(void)
{
    const uint32_t driver_faults = drv_comp_fault_mask();
    uint32_t app_faults = 0U;

    if ((driver_faults & DRV_FAULT_MOS_OCP) != 0U)
    {
        app_faults |= AURORA_FAULT_FAST_MOS_OCP;
    }
    if ((driver_faults & DRV_FAULT_PV_OCP) != 0U)
    {
        app_faults |= AURORA_FAULT_FAST_PV_OCP;
    }
    if (app_faults == 0U)
    {
        app_faults = AURORA_FAULT_FAST_BREAK;
    }

    /* service入口第一动作仍是恒定时间强制关波；这里只负责故障原因映射。 */
    aurora_service_isr_fast_fault(&g_aurora_service, app_faults);
    drv_comp_irq_ack();
}

void COMP0_IRQHandler(void)
{
    if (drv_pwm_break_latched())
    {
        drv_pwm_quiesce_break_irq_isr();
    }
    handle_fast_comparator_fault();
}

void COMP1_2_3_IRQHandler(void)
{
    handle_fast_comparator_fault();
}

void ATMR_BRK_UP_TRG_COM_IRQHandler(void)
{
    /* Break优先于Update；锁存位不在ISR内清除，只屏蔽重复Break中断。 */
    if (DDL_ATMR_IsActiveFlag_BRK(ATMR) != 0U)
    {
        drv_pwm_quiesce_break_irq_isr();
        handle_fast_comparator_fault();
    }
    if ((DDL_ATMR_IsEnabledIT_UPDATE(ATMR) != 0U) &&
        (DDL_ATMR_IsActiveFlag_UPDATE(ATMR) != 0U))
    {
        aurora_service_isr_pwm_update(&g_aurora_service);
    }
}

void USART_IRQHandler(void)
{
    uint32_t budget = BOARD_UART_ISR_RX_BUDGET;

    /* 单次ISR只搬运有限字节，避免通信流量长期饿死ADC和控制任务。 */
    while (drv_uart_rx_ready_isr() && (budget > 0U))
    {
        aurora_service_isr_uart_rx(&g_aurora_service, drv_uart_read_isr());
        budget--;
    }
    drv_uart_tx_isr();
    drv_uart_irq_ack();
}

void HardFault_Handler(void)
{
    drv_pwm_force_off_isr();
    drv_pwm_quiesce_break_irq_isr();
    drv_io_set_relay(false);
    drv_system_reset();
    for (;;)
    {
    }
}
