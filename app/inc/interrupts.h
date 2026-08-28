#ifndef AURORA_INTERRUPTS_H
#define AURORA_INTERRUPTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 系统节拍中断入口。 */
void SysTick_Handler(void);
/* ADC DMA中断入口。 */
void DMA_CH1_IRQHandler(void);
/* MOS快速比较器中断入口。 */
void COMP0_IRQHandler(void);
/* PV快速比较器组中断入口。 */
void COMP1_2_3_IRQHandler(void);
/* ATMR共享Break/Update中断入口。 */
void ATMR_BRK_UP_TRG_COM_IRQHandler(void);
/* 共享USART中断入口。 */
void USART_IRQHandler(void);
/* HardFault安全收尾入口。 */
void HardFault_Handler(void);

#ifdef __cplusplus
}
#endif

#endif
