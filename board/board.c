#include "board.h"

#include "app_types.h"
#include "board_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static void set_voltage_calibration(aurora_board_adc_calibration_t *calibration, int32_t divider_num, int32_t divider_den)
 * Input       : calibration - 标定输出；divider_num/divider_den - 硬件分压比
 * Output      : 无
 * Description : 根据ADC参考电压、满量程码和分压比生成单极性电压通道标定参数。
 *---------------------------------------------------------------------------*/
static void set_voltage_calibration(aurora_board_adc_calibration_t *calibration,
                                    int32_t divider_num,
                                    int32_t divider_den)
{
    calibration->kind = AURORA_ADC_CALIBRATION_LINEAR;
    calibration->gain_num = BOARD_ADC_REFERENCE_MV * divider_num;
    calibration->gain_den = BOARD_ADC_FULL_SCALE_CODE * divider_den;
    calibration->zero_code = 0;
    calibration->polarity = 1;
    calibration->valid = true;
}

/*---------------------------------------------------------------------------*
 * Name        : static void set_mos_ntc_calibration(aurora_board_adc_calibration_t *calibration)
 * Input       : calibration - 标定输出
 * Output      : 无
 * Description : 写入原理图板载MOS NTC的上拉、R25、Beta和有效温度范围参数。
 *---------------------------------------------------------------------------*/
static void set_mos_ntc_calibration(aurora_board_adc_calibration_t *calibration)
{
    calibration->kind = AURORA_ADC_CALIBRATION_NTC_BETA;
    calibration->valid = true;
    calibration->ntc_pullup_ohm = BOARD_NTC_MOS_PULLUP_OHM;
    calibration->ntc_r25_ohm = BOARD_NTC_MOS_R25_OHM;
    calibration->ntc_beta_kelvin = BOARD_NTC_MOS_BETA_KELVIN;
    calibration->ntc_full_scale_code = BOARD_ADC_FULL_SCALE_CODE;
    calibration->ntc_reference_temp_dc = BOARD_NTC_MOS_REFERENCE_TEMP_DC;
    calibration->ntc_min_temp_dc = BOARD_NTC_MOS_MIN_TEMP_DC;
    calibration->ntc_max_temp_dc = BOARD_NTC_MOS_MAX_TEMP_DC;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_board_get_adc_calibration(size_t channel, aurora_board_adc_calibration_t *calibration)
 * Input       : channel - 逻辑通道索引；calibration - 标定输出地址
 * Output      : true表示索引合法；false表示参数错误或索引越界
 * Description : 返回指定ADC逻辑通道的线性或NTC标定参数；环境NTC型号未确认时明确返回valid=false。
 *---------------------------------------------------------------------------*/
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
    case BOARD_ADC_INDEX_PV_I:
        /* 正向PV电流会使内部OPA输出码下降，因此polarity=-1。 */
        calibration->gain_num = BOARD_ADC_PV_I_GAIN_NUM;
        calibration->gain_den = BOARD_ADC_PV_I_GAIN_DEN;
        calibration->zero_code = BOARD_ADC_PV_I_ZERO_CODE;
        calibration->polarity = BOARD_ADC_PV_I_POLARITY;
        calibration->valid = true;
        break;

    case BOARD_ADC_INDEX_PV_U:
        set_voltage_calibration(calibration,
                                BOARD_ADC_PV_U_DIVIDER_NUM,
                                BOARD_ADC_PV_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_BAT_U:
        set_voltage_calibration(calibration,
                                BOARD_ADC_BAT_U_DIVIDER_NUM,
                                BOARD_ADC_BAT_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_BUS_U:
        set_voltage_calibration(calibration,
                                BOARD_ADC_BUS_U_DIVIDER_NUM,
                                BOARD_ADC_BUS_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_NTC_MOS:
        set_mos_ntc_calibration(calibration);
        break;

    case BOARD_ADC_INDEX_NTC_AMB:
    default:
        /* CON4只标注外接NTC，型号和B值未确认，禁止伪造环境温度。 */
        calibration->valid = false;
        break;
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_board_power_gate_open(void)
 * Input       : 无
 * Output      : true表示所有人工门禁均已放行；false表示必须保持PWM关闭
 * Description : 汇总PinMap、比较器、模拟标定、Keil和低压台架证据；代码整理不得绕过任一门禁。
 *---------------------------------------------------------------------------*/
bool aurora_board_power_gate_open(void)
{
    return (BOARD_POWER_OUTPUT_ALLOWED != 0U) &&
           (BOARD_GATE_PINMAP_REVIEWED != 0U) &&
           (BOARD_GATE_COMP_ROUTE_VALIDATED != 0U) &&
           (BOARD_GATE_ANALOG_CALIBRATED != 0U) &&
           (BOARD_GATE_KEIL_LINKED != 0U) &&
           (BOARD_GATE_LOW_VOLTAGE_BENCH != 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_board_flash_page_a(void)
 * Input       : 无
 * Output      : Flash Journal A页起始地址
 * Description : 返回链接脚本保留的A页地址，供Service读取和编程。
 *---------------------------------------------------------------------------*/
uint32_t aurora_board_flash_page_a(void)
{
    return BOARD_FLASH_PAGE_A_ADDRESS;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_board_flash_page_b(void)
 * Input       : 无
 * Output      : Flash Journal B页起始地址
 * Description : 返回链接脚本保留的B页地址，供Service读取和编程。
 *---------------------------------------------------------------------------*/
uint32_t aurora_board_flash_page_b(void)
{
    return BOARD_FLASH_PAGE_B_ADDRESS;
}
