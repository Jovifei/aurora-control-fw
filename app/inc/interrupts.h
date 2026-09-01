#ifndef AURORA_INTERRUPTS_H
#define AURORA_INTERRUPTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M0+目标中断入口；只做Driver应答、快速关波和事件投递。 */
void SysTick_Handler(void);
void ADC_IRQHandler(void);
void DMA_CH1_IRQHandler(void);
void COMP0_IRQHandler(void);
void COMP1_2_3_IRQHandler(void);
void ATMR_BRK_UP_TRG_COM_IRQHandler(void);
void USART_IRQHandler(void);
void HardFault_Handler(void);

#ifdef __cplusplus
}
#endif

#endif
