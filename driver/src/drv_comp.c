#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_comp0.h"
#include "g32f031_ddl_comp1.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_opa.h"
#include "g32f031_ddl_rcc.h"

/*---------------------------------------------------------------------------*
 * Name        : static void configure_analog(GPIO_TypeDef *port, uint32_t pins)
 * Input       : port - GPIO端口；pins - GPIO引脚集合
 * Output      : 无
 * Description : 把指定GPIO集合配置为模拟模式，供OPA/COMP输入使用。
 *---------------------------------------------------------------------------*/
static void configure_analog(GPIO_TypeDef *port, uint32_t pins)
{
    DDL_GPIO_InitTypeDef gpio = {0U};
    gpio.Pin = pins;
    gpio.Mode = DDL_GPIO_MODE_ANALOG;
    gpio.Drive = DDL_GPIO_DRIVE_LOW;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(port, &gpio);
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_ENABLE);
}

/*---------------------------------------------------------------------------*
 * Name        : static void configure_comp0_output_pb10(void)
 * Input       : 无
 * Output      : 无
 * Description : 把PB10配置为AF7开漏COMP0_OUT，使低有效故障能够直接拉低门极驱动EN。
 *---------------------------------------------------------------------------*/
static void configure_comp0_output_pb10(void)
{
    DDL_GPIO_InitTypeDef gpio = {0U};

    /* 官方复用表：PB10 / AF7 = COMP0_OUT；该网络直连门极驱动 U6 EN。 */
    gpio.Pin = DDL_GPIO_PIN_10;
    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_OPENDRAIN;
    gpio.InputEnable = DDL_GPIO_INPUT_DISABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_7;
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOB, &gpio);
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_ENABLE);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_comp_init(void)
 * Input       : 无
 * Output      : true表示OPA、COMP、PB10输出和中断配置成功；false表示初始化失败
 * Description :
 * 配置两路内部OPA、MOS/PV快速比较器、PB10低有效COMP0_OUT及比较器中断；保持硬件保护极性与ATMR
 * Break一致。
 *---------------------------------------------------------------------------*/
bool drv_comp_init(void)
{
    DDL_OPA_InitTypeDef opa;
    DDL_COMP0_InitTypeDef comp0;
    DDL_COMP1_InitTypeDef comp2;

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA | DDL_AHB_GRP1_PERIPH_GPIOB);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_OPA | DDL_APB_GRP1_PERIPH_COMP0 |
                             DDL_APB_GRP1_PERIPH_COMP1);
    DDL_RCC_Lock();

    /*
     * PA0~PA5：两组内部OPA的输出/差分输入；PA7：COMP0参考。
     * PB2/PB6：COMP2差分输入。模拟引脚不配置数字上下拉。
     */
    configure_analog(GPIOA, DDL_GPIO_PIN_0 | DDL_GPIO_PIN_1 | DDL_GPIO_PIN_2 | DDL_GPIO_PIN_3 |
                                DDL_GPIO_PIN_4 | DDL_GPIO_PIN_5 | DDL_GPIO_PIN_7);
    configure_analog(GPIOB, DDL_GPIO_PIN_2 | DDL_GPIO_PIN_6);
    configure_comp0_output_pb10();

    /* OPA0测MOS电流、OPA1测PV电流，均使用内部16倍增益与半电源偏置。 */
    DDL_OPA_StructInit(&opa);
    opa.Channel = DDL_OPA_CHANNEL_0;
    opa.GainSelect = DDL_OPA_INTERNALGAIN_16;
    opa.InputControl = DDL_OPA_INCTRL_ENABLE;
    opa.OutputControl = DDL_OPA_OUTCTRL_ENABLE;
    opa.VCMSelect = DDL_OPA_VCMSEL_AVDD_0_5;
    if (DDL_OPA_Init(OPA, &opa) != SUCCESS)
    {
        return false;
    }
    opa.Channel = DDL_OPA_CHANNEL_1;
    if (DDL_OPA_Init(OPA, &opa) != SUCCESS)
    {
        return false;
    }
    DDL_OPA_Enable(OPA, DDL_OPA_CHANNEL_0 | DDL_OPA_CHANNEL_1);

    /* COMP0：MOS分流电流与PA7参考比较；外部EN和内部Break均按低有效故障。 */
    DDL_COMP0_StructInit(&comp0);
    comp0.InputPlus = DDL_COMP0_INPUT_PLUS_PA7;
    comp0.InputMinus = DDL_COMP0_INPUT_MINUS_PGA0;
    comp0.OutputPol = DDL_COMP0_OUTPUTPOL_INVERTED;
    comp0.FilterPSC = DDL_COMP0_FILTERPSC_1;
    comp0.FilterCFG = DDL_COMP0_FILTERCFG_2;
    comp0.HsyP = DDL_COMP0_HYSP_20MV;
    comp0.HsyN = DDL_COMP0_HYSN_20MV;
    if (DDL_COMP0_Init(COMP0, &comp0) != SUCCESS)
    {
        return false;
    }
    DDL_COMP0_SetInterrupt(COMP0, DDL_COMP0_EDGE_INT_FALLING);
    DDL_COMP0_ClearFlag_IT(COMP0);
    DDL_COMP0_Enable(COMP0);

    /* COMP2：PB6参考与PB2输入电流快速信号比较。 */
    DDL_COMP1_StructInit(&comp2);
    comp2.InputPlus = DDL_COMP1_INPUT_PLUS_PB6;
    comp2.InputMinus = DDL_COMP1_INPUT_MINUS_PB2;
    comp2.OutputPol = DDL_COMP1_OUTPUTPOL_NONINVERTED;
    comp2.FilterPSC = DDL_COMP1_FILTERPSC_1;
    comp2.FilterCFG = DDL_COMP1_FILTERCFG_2;
    comp2.HsyP = DDL_COMP1_HYSP_20MV;
    comp2.HsyN = DDL_COMP1_HYSN_20MV;
    if (DDL_COMP1_Init(COMP2, &comp2) != SUCCESS)
    {
        return false;
    }
    DDL_COMP1_SetInterrupt(COMP2, DDL_COMP1_EDGE_INT_RISING);
    DDL_COMP1_ClearFlag_IT(COMP2);
    DDL_COMP1_Enable(COMP2);

    NVIC_EnableIRQ(COMP0_IRQn);
    NVIC_EnableIRQ(COMP1_2_3_IRQn);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_comp_fault_mask(void)
 * Input       : 无
 * Output      : DRV_FAULT_*快速故障位图
 * Description : 结合比较器中断标志和实时输出电平生成驱动层快速故障位图。
 *---------------------------------------------------------------------------*/
uint32_t drv_comp_fault_mask(void)
{
    uint32_t mask = 0U;
    if ((DDL_COMP0_IsActiveFlag_IT(COMP0) != 0U) || (DDL_COMP0_ReadOutputLevel(COMP0) == 0U))
    {
        mask |= DRV_FAULT_MOS_OCP;
    }
    if ((DDL_COMP1_IsActiveFlag_IT(COMP2) != 0U) || (DDL_COMP1_ReadOutputLevel(COMP2) != 0U))
    {
        mask |= DRV_FAULT_PV_OCP;
    }
    return mask;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_comp_irq_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : 清除已置位的比较器中断标志，不执行恢复或重新使能PWM。
 *---------------------------------------------------------------------------*/
void drv_comp_irq_ack(void)
{
    if (DDL_COMP0_IsActiveFlag_IT(COMP0) != 0U)
    {
        DDL_COMP0_ClearFlag_IT(COMP0);
    }
    if (DDL_COMP1_IsActiveFlag_IT(COMP2) != 0U)
    {
        DDL_COMP1_ClearFlag_IT(COMP2);
    }
}
