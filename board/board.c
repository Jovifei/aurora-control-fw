#include "board.h"

#include "board_config.h"

#include <string.h>

bool aurora_board_get_adc_calibration(size_t channel,
                                      aurora_board_adc_calibration_t *calibration)
{
    if ((calibration == NULL) || (channel >= 6U))
    {
        return false;
    }
    memset(calibration, 0, sizeof(*calibration));

    switch (channel)
    {
    case 0U: /* PV_I：3mΩ × 内部OPA 16倍，零点约VDD/2，正向电流使码值下降。 */
        calibration->gain_num = 16790;
        calibration->gain_den = 1000;
        calibration->zero_code = 2048;
        calibration->polarity = -1;
        calibration->valid = true;
        break;
    case 1U: /* PV_U：75k/3k，分压比26。 */
        calibration->gain_num = 3300 * 26;
        calibration->gain_den = 4095;
        calibration->polarity = 1;
        calibration->valid = true;
        break;
    case 2U: /* BAT_U：15M/510k，分压比15510/510。 */
        calibration->gain_num = 3300 * 15510;
        calibration->gain_den = 4095 * 510;
        calibration->polarity = 1;
        calibration->valid = true;
        break;
    case 3U: /* BST_U：125k/5k，分压比26。 */
        calibration->gain_num = 3300 * 26;
        calibration->gain_den = 4095;
        calibration->polarity = 1;
        calibration->valid = true;
        break;
    case 4U: /* NTC_MOS：阻值/B值和板级标定完成前不得参与功率放行。 */
    case 5U: /* NTC_AMB：同上。 */
    default:
        calibration->valid = false;
        break;
    }
    return true;
}

bool aurora_board_power_gate_open(void)
{
    return (BOARD_POWER_OUTPUT_ALLOWED != 0U) &&
           (BOARD_GATE_PINMAP_REVIEWED != 0U) &&
           (BOARD_GATE_COMP_ROUTE_VALIDATED != 0U) &&
           (BOARD_GATE_ANALOG_CALIBRATED != 0U) &&
           (BOARD_GATE_KEIL_LINKED != 0U) &&
           (BOARD_GATE_LOW_VOLTAGE_BENCH != 0U);
}

uint32_t aurora_board_flash_page_a(void)
{
    return BOARD_FLASH_PAGE_A_ADDRESS;
}

uint32_t aurora_board_flash_page_b(void)
{
    return BOARD_FLASH_PAGE_B_ADDRESS;
}
