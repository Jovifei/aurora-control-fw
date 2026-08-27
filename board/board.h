#ifndef AURORA_BOARD_H
#define AURORA_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int32_t gain_num;
    int32_t gain_den;
    int32_t offset;
    int16_t zero_code;
    int8_t polarity;
    bool valid;
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
