#ifndef AURORA_DRV_ADC_H
#define AURORA_DRV_ADC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ADC逻辑通道数，必须与board_config扫描顺序一致。 */
#define DRV_ADC_CHANNEL_COUNT                       (6U)
/* 每个DMA半缓冲的完整扫描次数。 */
#define DRV_ADC_SCANS_PER_BLOCK                     (16U)
/* 单个DMA完成块的16位字数。 */
#define DRV_ADC_BLOCK_WORDS                         (DRV_ADC_CHANNEL_COUNT * \
                                                     DRV_ADC_SCANS_PER_BLOCK)
/* ADC DMA结果位：半缓冲0完成。 */
#define DRV_ADC_IRQ_BLOCK0                          (1U << 0)
/* ADC DMA结果位：半缓冲1完成。 */
#define DRV_ADC_IRQ_BLOCK1                          (1U << 1)
/* ADC DMA结果位：传输错误。 */
#define DRV_ADC_IRQ_ERROR                           (1U << 2)

void BSP_ADC_Init(void);
void BSP_ADC_Start(void);
uint16_t BSP_ADC_GetRaw(uint32_t channel_index);
uint16_t BSP_ADC_GetAverage(uint32_t channel_index);
void BSP_ADC_IRQHandler(void);
uint32_t BSP_ADC_GetSequenceCount(void);

bool drv_adc_init(void);
bool drv_adc_start(void);
const uint16_t *drv_adc_completed_block(uint8_t block_index);
size_t drv_adc_block_words(void);
uint8_t drv_adc_dma_irq_ack(void);

#endif
