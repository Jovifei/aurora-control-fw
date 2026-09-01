#include "drv_board.h"

#include "board_config.h"

#if defined(AURORA_HOST_TEST_POWER_GATES_OPEN) && !defined(AURORA_HOST_TEST)
#error "AURORA_HOST_TEST_POWER_GATES_OPEN is forbidden in target firmware"
#endif

/*---------------------------------------------------------------------------*
 * Name        : static void clear_calibration(
 *               drv_board_adc_calibration_t *calibration)
 * Input       : calibration - 标定结构地址
 * Output      : 无
 * Description : 显式清零标定字段，避免目标Driver为小结构初始化依赖C库memset。
 *---------------------------------------------------------------------------*/
static void clear_calibration(drv_board_adc_calibration_t *calibration)
{
    calibration->gain_num = 0;
    calibration->gain_den = 0;
    calibration->offset = 0;
    calibration->zero_code = 0;
    calibration->polarity = 0;
    calibration->valid = false;
}

/*---------------------------------------------------------------------------*
 * Name        : static void set_voltage_calibration(
 *               drv_board_adc_calibration_t *calibration,
 *               int32_t divider_num, int32_t divider_den)
 * Input       : calibration - 标定输出；divider_num/divider_den - 硬件分压比
 * Output      : 无
 * Description : 根据ADC参考电压、满量程码和分压比生成单极性电压通道标定参数。
 *---------------------------------------------------------------------------*/
static void set_voltage_calibration(drv_board_adc_calibration_t *calibration, int32_t divider_num,
                                    int32_t divider_den)
{
    calibration->gain_num = BOARD_ADC_REFERENCE_MV * divider_num;
    calibration->gain_den = BOARD_ADC_FULL_SCALE_CODE * divider_den;
    calibration->zero_code = 0;
    calibration->polarity = 1;
    calibration->valid = true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_board_get_adc_calibration(size_t channel,
 *               drv_board_adc_calibration_t *calibration)
 * Input       : channel - 逻辑通道索引；calibration - 标定输出地址
 * Output      : true表示索引合法；false表示参数错误或索引越界
 * Description : 返回电压/电流通道线性标定；NTC由Measurement直接使用原始码查100K/B3950表。
 *---------------------------------------------------------------------------*/
bool drv_board_get_adc_calibration(size_t channel, drv_board_adc_calibration_t *calibration)
{
    if ((calibration == NULL) || (channel >= 6U))
    {
        return false;
    }

    clear_calibration(calibration);

    switch (channel)
    {
    case BOARD_ADC_INDEX_PV_I:
        /* 内部OPA×16使用AVDD/2共模，零电流码按BOARD_ADC_PV_I_ZERO_CODE装载。 */
        calibration->gain_num = BOARD_ADC_PV_I_GAIN_NUM;
        calibration->gain_den = BOARD_ADC_PV_I_GAIN_DEN;
        calibration->zero_code = BOARD_ADC_PV_I_ZERO_CODE;
        calibration->polarity = BOARD_ADC_PV_I_POLARITY;
        calibration->valid = true;
        break;

    case BOARD_ADC_INDEX_PV_U:
        set_voltage_calibration(calibration, BOARD_ADC_PV_U_DIVIDER_NUM,
                                BOARD_ADC_PV_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_BAT_U:
        set_voltage_calibration(calibration, BOARD_ADC_BAT_U_DIVIDER_NUM,
                                BOARD_ADC_BAT_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_BUS_U:
        set_voltage_calibration(calibration, BOARD_ADC_BUS_U_DIVIDER_NUM,
                                BOARD_ADC_BUS_U_DIVIDER_DEN);
        break;

    case BOARD_ADC_INDEX_NTC_MOS:
    case BOARD_ADC_INDEX_NTC_AMB:
    default:
        calibration->valid = false;
        break;
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_board_power_gate_open(void)
 * Input       : 无
 * Output      : true表示所有人工门禁均已放行；false表示必须保持PWM关闭
 * Description : 汇总PinMap、比较器、模拟标定、Keil和低压台架证据；软件重构不得绕过任一门禁。
 *---------------------------------------------------------------------------*/
bool drv_board_power_gate_open(void)
{
#if defined(AURORA_HOST_TEST) && defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    // 仅v0.10.3端到端Host目标使用；生产目标仍完全由BOARD_GATE_*与总门控制。
    return true;
#else
    return (BOARD_POWER_OUTPUT_ALLOWED != 0U) && (BOARD_GATE_PINMAP_REVIEWED != 0U) &&
           (BOARD_GATE_COMP_ROUTE_VALIDATED != 0U) && (BOARD_GATE_ANALOG_CALIBRATED != 0U) &&
           (BOARD_GATE_KEIL_LINKED != 0U) && (BOARD_GATE_LOW_VOLTAGE_BENCH != 0U);
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_board_demo_load_gate_open(void)
 * Input       : 无
 * Output      : true表示Demo负载/空载/短路/外部电源台架已人工验收
 * Description : 仅供Demo模式PWM和Relay闭合复核；Host覆盖只允许存在于专用测试目标。
 *---------------------------------------------------------------------------*/
bool drv_board_demo_load_gate_open(void)
{
#if defined(AURORA_HOST_TEST) && defined(AURORA_HOST_TEST_POWER_GATES_OPEN)
    return true;
#else
    return (BOARD_GATE_DEMO_LOAD_VALIDATED != 0U);
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_board_flash_page_a(void)
 * Input       : 无
 * Output      : Flash Journal A页起始地址
 * Description : 返回链接脚本保留的A页地址，供应用运行层通过Driver契约读取和编程。
 *---------------------------------------------------------------------------*/
uint32_t drv_board_flash_page_a(void)
{
    return BOARD_FLASH_PAGE_A_ADDRESS;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_board_flash_page_b(void)
 * Input       : 无
 * Output      : Flash Journal B页起始地址
 * Description : 返回链接脚本保留的B页地址，供应用运行层通过Driver契约读取和编程。
 *---------------------------------------------------------------------------*/
uint32_t drv_board_flash_page_b(void)
{
    return BOARD_FLASH_PAGE_B_ADDRESS;
}
