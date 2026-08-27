#include "driver.h"

#include "g32f031xx.h"
#include "system_g32f031.h"

static volatile uint32_t g_system_ms;

void drv_system_init(void)
{
    SystemCoreClockUpdate();
    g_system_ms = 0U;
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

uint32_t drv_time_now_ms(void)
{
    return g_system_ms;
}

void drv_time_tick_isr(void)
{
    g_system_ms++;
}

aurora_irq_state_t drv_irq_save(void)
{
    aurora_irq_state_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

void drv_irq_restore(aurora_irq_state_t state)
{
    __set_PRIMASK(state);
}

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

void drv_system_reset(void)
{
    NVIC_SystemReset();
}
