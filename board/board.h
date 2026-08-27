#ifndef AURORA_BOARD_H
#define AURORA_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* board层向Service提供的硬件无关ADC标定描述。 */
typedef struct
{
    int32_t gain_num;                                /* 物理量/码的比例分子。 */
    int32_t gain_den;                                /* 比例分母，不得为0。 */
    int32_t offset;                                  /* 换算后固定偏移。 */
    int16_t zero_code;                               /* 双向电流通道零点码。 */
    int8_t polarity;                                 /* +1同向，-1反向。 */
    bool valid;                                      /* true表示已完成标定。 */
} aurora_board_adc_calibration_t;

bool aurora_board_get_adc_calibration(size_t channel,
                                      aurora_board_adc_calibration_t *calibration);
bool aurora_board_power_gate_open(void);
uint32_t aurora_board_flash_page_a(void);
uint32_t aurora_board_flash_page_b(void);

#ifdef __cplusplus
}
#endif

#endif
