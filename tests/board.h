#ifndef AURORA_TEST_BOARD_COMPAT_H
#define AURORA_TEST_BOARD_COMPAT_H

/* 仅供旧Host测试兼容；生产board目录已经合并到driver。 */
#include "drv_board.h"

typedef drv_board_adc_calibration_t aurora_board_adc_calibration_t;

#define aurora_board_get_adc_calibration            drv_board_get_adc_calibration /* 测试兼容标定别名。 */
#define aurora_board_power_gate_open                drv_board_power_gate_open /* 测试兼容功率门别名。 */
#define aurora_board_demo_load_gate_open            drv_board_demo_load_gate_open /* 测试兼容Demo门别名。 */
#define aurora_board_flash_page_a                   drv_board_flash_page_a /* 测试兼容Flash A别名。 */
#define aurora_board_flash_page_b                   drv_board_flash_page_b /* 测试兼容Flash B别名。 */

#endif
