#ifndef AURORA_DRV_ADC_H
#define AURORA_DRV_ADC_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化定时器触发、多通道扫描和DMA双半缓冲。 */
bool drv_adc_init(void);
/* 启动ADC/DMA采样链。 */
bool drv_adc_start(void);
/* 返回指定DMA半缓冲的完整扫描数据。 */
const uint16_t *drv_adc_completed_block(uint8_t block_index);
/* 返回单个DMA完成块的16位字数。 */
size_t drv_adc_block_words(void);
/* 应答ADC DMA中断并返回完成/错误位。 */
uint8_t drv_adc_dma_irq_ack(void);

#ifdef __cplusplus
}
#endif

#endif
