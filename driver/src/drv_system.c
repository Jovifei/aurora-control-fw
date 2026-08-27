#include "driver.h"

#include "g32f031xx.h"
#include "system_g32f031.h"

static volatile uint32_t g_system_ms;

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 更新系统时钟、清零毫秒计数并配置1 ms SysTick。
 *---------------------------------------------------------------------------*/
void drv_system_init(void)
{
    SystemCoreClockUpdate();
    g_system_ms = 0U;
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_time_now_ms(void)
 * Input       : 无
 * Output      : 系统毫秒时间戳
 * Description : 返回由SysTick维护的系统毫秒计数。
 *---------------------------------------------------------------------------*/
uint32_t drv_time_now_ms(void)
{
    return g_system_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_time_tick_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 在SysTick ISR中递增系统毫秒计数。
 *---------------------------------------------------------------------------*/
void drv_time_tick_isr(void)
{
    g_system_ms++;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_irq_state_t drv_irq_save(void)
 * Input       : 无
 * Output      : 调用前的PRIMASK状态
 * Description : 保存PRIMASK并关闭全局中断，供极短临界区使用。
 *---------------------------------------------------------------------------*/
aurora_irq_state_t drv_irq_save(void)
{
    aurora_irq_state_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_restore(aurora_irq_state_t state)
 * Input       : state - 目标状态
 * Output      : 无
 * Description : 恢复调用前的PRIMASK中断状态。
 *---------------------------------------------------------------------------*/
void drv_irq_restore(aurora_irq_state_t state)
{
    __set_PRIMASK(state);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_configure_priorities(void)
 * Input       : 无
 * Output      : 无
 * Description : 按快速故障、DMA、SysTick、UART顺序配置NVIC优先级。
 *---------------------------------------------------------------------------*/
void drv_irq_configure_priorities(void)
{
    /* 数值越小优先级越高：快速故障必须抢占PWM更新、DMA和通信。 */
    NVIC_SetPriority(COMP0_IRQn, 0U);
    NVIC_SetPriority(COMP1_2_3_IRQn, 0U);
    NVIC_SetPriority(ATMR_BRK_UP_TRG_COM_IRQn, 0U);
    NVIC_SetPriority(DMA_CH1_IRQn, 1U);
    NVIC_SetPriority(SysTick_IRQn, 2U);
    NVIC_SetPriority(USART_IRQn, 3U);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_reset(void)
 * Input       : 无
 * Output      : 无
 * Description : 请求Cortex-M系统复位。
 *---------------------------------------------------------------------------*/
void drv_system_reset(void)
{
    NVIC_SystemReset();
}
