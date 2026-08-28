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
__attribute__((noreturn)) void HardFault_Handler(void);

/*---------------------------------------------------------------------------*
 * Name        : static void handle_fast_comparator_fault(void)
 * Input       : 无
 * Output      : 无
 * Description : 映射COMP故障原因并交给Service按“PWM是否实际输出”决定诊断或锁存；ISR本身不做恢复。
 *---------------------------------------------------------------------------*/
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

    /* 硬件关波能力始终保留；PWM未真正输出时不把上电/零点瞬态软件锁存为OCP。 */
    aurora_service_isr_comparator_fault(&g_aurora_service, app_faults);
    drv_comp_irq_ack();
}

/*---------------------------------------------------------------------------*
 * Name        : void SysTick_Handler(void)
 * Input       : 无
 * Output      : 无
 * Description : 处理1ms SysTick，只更新时间并投递Service节拍事件。
 *---------------------------------------------------------------------------*/
void SysTick_Handler(void)
{
    aurora_service_isr_tick(&g_aurora_service);
}

/*---------------------------------------------------------------------------*
 * Name        : void DMA_CH1_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : 处理ADC DMA半传输、全传输和错误标志，把完成块或DMA故障发布给Service。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : void COMP0_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : 处理MOS快速过流比较器中断；Break锁存保留到主循环按策略确认。
 *---------------------------------------------------------------------------*/
void COMP0_IRQHandler(void)
{
    if (drv_pwm_break_latched())
    {
        drv_pwm_quiesce_break_irq_isr();
    }
    handle_fast_comparator_fault();
}

/*---------------------------------------------------------------------------*
 * Name        : void COMP1_2_3_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : 处理PV快速过流比较器组中断并交给统一比较器故障桥接。
 *---------------------------------------------------------------------------*/
void COMP1_2_3_IRQHandler(void)
{
    handle_fast_comparator_fault();
}

/*---------------------------------------------------------------------------*
 * Name        : void ATMR_BRK_UP_TRG_COM_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : 处理ATMR共享Break/Update向量；始终先处理Break，再确认一次性0 CCR自然Update。
 *---------------------------------------------------------------------------*/
void ATMR_BRK_UP_TRG_COM_IRQHandler(void)
{
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

/*---------------------------------------------------------------------------*
 * Name        : void USART_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : 按固定字节预算搬运RX、推进TX并清错误标志，避免通信ISR长期占用CPU。
 *---------------------------------------------------------------------------*/
void USART_IRQHandler(void)
{
    uint32_t budget = BOARD_UART_ISR_RX_BUDGET;

    while (drv_uart_rx_ready_isr() && (budget > 0U))
    {
        aurora_service_isr_uart_rx(&g_aurora_service, drv_uart_read_isr());
        budget--;
    }
    drv_uart_tx_isr();
    drv_uart_irq_ack();
}

/*---------------------------------------------------------------------------*
 * Name        : void HardFault_Handler(void)
 * Input       : 无
 * Output      : 无
 * Description : HardFault立即关PWM、屏蔽Break风暴、断继电器并请求系统复位。
 *---------------------------------------------------------------------------*/
__attribute__((noreturn)) void HardFault_Handler(void)
{
    drv_pwm_force_off_isr();
    drv_pwm_quiesce_break_irq_isr();
    drv_io_set_relay(false);
    drv_system_reset();
}
