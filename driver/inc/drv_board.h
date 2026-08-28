#ifndef AURORA_DRV_BOARD_H
#define AURORA_DRV_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Driver层向APP提供的硬件无关ADC标定描述。 */
typedef struct
{
    int32_t gain_num;                                /* 物理量/码比例分子。 */
    int32_t gain_den;                                /* 比例分母，不得为0。 */
    int32_t offset;                                  /* 换算后固定偏移。 */
    int16_t zero_code;                               /* 双向电流通道零点码。 */
    int8_t polarity;                                 /* +1同向，-1反向。 */
    bool valid;                                      /* true表示该线性标定可用。 */
} drv_board_adc_calibration_t;

bool drv_board_get_adc_calibration(size_t channel,
                                   drv_board_adc_calibration_t *calibration);
bool drv_board_power_gate_open(void);
uint32_t drv_board_flash_page_a(void);
uint32_t drv_board_flash_page_b(void);

#endif
