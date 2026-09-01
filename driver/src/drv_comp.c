#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_comp0.h"
#include "g32f031_ddl_comp1.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_opa.h"
#include "g32f031_ddl_rcc.h"

static volatile uint32_t s_comp0_event_count;
static volatile uint32_t s_comp2_event_count;

/*---------------------------------------------------------------------------*
 * Name        : void BSP_OPA_Init(void)
 * Input       : 无
 * Output      : 无
 * Description : 按官方Application初始化OPA0（PA0/1/2）与OPA1（PA3/4/5），内部增益x16。
 *---------------------------------------------------------------------------*/
void BSP_OPA_Init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0};
    DDL_OPA_InitTypeDef opa = {0};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_OPA);
    DDL_RCC_Lock();

    /* OPA0：PA0/1/2，内部增益x16 */
    gpio.Pin = DDL_GPIO_PIN_0 | DDL_GPIO_PIN_1 | DDL_GPIO_PIN_2;
    gpio.Mode = DDL_GPIO_MODE_ANALOG;
    gpio.Drive = DDL_GPIO_DRIVE_LOW;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_DISABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);
    opa.Channel = DDL_OPA_CHANNEL_0;
    opa.GainSelect = DDL_OPA_INTERNALGAIN_16;
    opa.InputControl = DDL_OPA_INCTRL_ENABLE;
    opa.OutputControl = DDL_OPA_OUTCTRL_ENABLE;
    opa.VCMSelect = DDL_OPA_VCMSEL_AVDD_0_5;
    (void)DDL_OPA_Init(OPA, &opa);
    DDL_OPA_Enable(OPA, DDL_OPA_CHANNEL_0);

    /* OPA1：PA3/4/5，内部增益x16 */
    gpio.Pin = DDL_GPIO_PIN_3 | DDL_GPIO_PIN_4 | DDL_GPIO_PIN_5;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);
    opa.Channel = DDL_OPA_CHANNEL_1;
    opa.GainSelect = DDL_OPA_INTERNALGAIN_16;
    (void)DDL_OPA_Init(OPA, &opa);
    DDL_OPA_Enable(OPA, DDL_OPA_CHANNEL_1);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_COMP_Init(void)
 * Input       : 无
 * Output      : 无
 * Description : 按原理图配置COMP0（PA7+/PGA0-、PB10开漏）与COMP2（PB6+/PB2-）。
 *               电流接负端，非反相输出过流为低，经R55上拉拉低U6 EN。
 *---------------------------------------------------------------------------*/
void BSP_COMP_Init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0};
    DDL_COMP0_InitTypeDef comp = {0};
    DDL_COMP1_InitTypeDef comp2 = {0};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA | DDL_AHB_GRP1_PERIPH_GPIOB);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_COMP0 | DDL_APB_GRP1_PERIPH_COMP1);
    DDL_RCC_Lock();

    /* COMP0：PA7正端 */
    gpio.Mode = DDL_GPIO_MODE_ANALOG;
    gpio.Drive = DDL_GPIO_DRIVE_LOW;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DDL_GPIO_PIN_7;
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);

    /* PB10：AF7 COMP0_OUT；开漏拉低U6 EN，官方示例为推挽 */
    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_OPENDRAIN;
    gpio.InputEnable = DDL_GPIO_INPUT_DISABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_7;
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DDL_GPIO_PIN_10;
    DDL_GPIO_Init(GPIOB, &gpio);
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_ENABLE);

    comp.InputPlus = DDL_COMP0_INPUT_PLUS_PA7;
    comp.InputMinus = DDL_COMP0_INPUT_MINUS_PGA0;
    /* 原理图：OPA0_O接COMP0负端，COMP0_P为VDD分压参考；非反相时过流输出为低。 */
    comp.OutputPol = DDL_COMP0_OUTPUTPOL_NONINVERTED;
    comp.FilterPSC = DDL_COMP0_FILTERPSC_1;
    comp.FilterCFG = DDL_COMP0_FILTERCFG_1;
    comp.HsyP = DDL_COMP0_HYSP_DISABLE;
    comp.HsyN = DDL_COMP0_HYSN_DISABLE;
    (void)DDL_COMP0_Init(COMP0, &comp);
    DDL_COMP0_SetInterrupt(COMP0, DDL_COMP0_EDGE_INT_RISING_FALLING);
    DDL_COMP0_ClearFlag_IT(COMP0);
    DDL_COMP0_Enable(COMP0);

    /* COMP2：原理图PB6正端参考、PB2负端接OPA1_O/PV电流 */
    gpio.Mode = DDL_GPIO_MODE_ANALOG;
    gpio.Drive = DDL_GPIO_DRIVE_LOW;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DDL_GPIO_PIN_2 | DDL_GPIO_PIN_6;
    DDL_GPIO_Init(GPIOB, &gpio);
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_ENABLE);
    comp2.InputPlus = DDL_COMP1_INPUT_PLUS_PB6;
    comp2.InputMinus = DDL_COMP1_INPUT_MINUS_PB2;
    comp2.OutputPol = DDL_COMP1_OUTPUTPOL_NONINVERTED;
    comp2.FilterPSC = DDL_COMP1_FILTERPSC_1;
    comp2.FilterCFG = DDL_COMP1_FILTERCFG_1;
    comp2.HsyP = DDL_COMP1_HYSP_DISABLE;
    comp2.HsyN = DDL_COMP1_HYSN_DISABLE;
    (void)DDL_COMP1_Init(COMP2, &comp2);
    DDL_COMP1_SetInterrupt(COMP2, DDL_COMP1_EDGE_INT_RISING_FALLING);
    DDL_COMP1_ClearFlag_IT(COMP2);
    DDL_COMP1_Enable(COMP2);

    NVIC_SetPriority(COMP0_IRQn, 0U);
    NVIC_EnableIRQ(COMP0_IRQn);
    NVIC_SetPriority(COMP1_2_3_IRQn, 0U);
    NVIC_EnableIRQ(COMP1_2_3_IRQn);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_COMP_IsActive(void)
 * Input       : 无
 * Output      : COMP0输出电平（非0=高）
 * Description : 轮询COMP0当前输出。非反相时高表示无MOS过流。
 *---------------------------------------------------------------------------*/
uint32_t BSP_COMP_IsActive(void)
{
    return DDL_COMP0_ReadOutputLevel(COMP0);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_COMP2_IsActive(void)
 * Input       : 无
 * Output      : COMP2输出电平（非0=高）
 * Description : 轮询COMP2当前输出，低表示PV过流比较结果有效。
 *---------------------------------------------------------------------------*/
uint32_t BSP_COMP2_IsActive(void)
{
    return DDL_COMP1_ReadOutputLevel(COMP2);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_COMP0_GetEventCount(void)
 * Input       : 无
 * Output      : COMP0边沿中断累计次数
 * Description : 诊断COMP0触发频率。
 *---------------------------------------------------------------------------*/
uint32_t BSP_COMP0_GetEventCount(void)
{
    return s_comp0_event_count;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_COMP2_GetEventCount(void)
 * Input       : 无
 * Output      : COMP2边沿中断累计次数
 * Description : 诊断COMP2触发频率。
 *---------------------------------------------------------------------------*/
uint32_t BSP_COMP2_GetEventCount(void)
{
    return s_comp2_event_count;
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_COMP0_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : COMP0边沿中断：清标志并递增事件计数；不在此恢复PWM。
 *---------------------------------------------------------------------------*/
void BSP_COMP0_IRQHandler(void)
{
    if (DDL_COMP0_IsActiveFlag_IT(COMP0) != 0U)
    {
        DDL_COMP0_ClearFlag_IT(COMP0);
        ++s_comp0_event_count;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_COMP2_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : COMP2边沿中断：清标志并递增事件计数；不在此恢复PWM。
 *---------------------------------------------------------------------------*/
void BSP_COMP2_IRQHandler(void)
{
    if (DDL_COMP1_IsActiveFlag_IT(COMP2) != 0U)
    {
        DDL_COMP1_ClearFlag_IT(COMP2);
        ++s_comp2_event_count;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_comp_init(void)
 * Input       : 无
 * Output      : true表示OPA与比较器初始化完成
 * Description : 按官方顺序调用BSP_OPA_Init与BSP_COMP_Init。
 *---------------------------------------------------------------------------*/
bool drv_comp_init(void)
{
    s_comp0_event_count = 0U;
    s_comp2_event_count = 0U;
    BSP_OPA_Init();
    BSP_COMP_Init();
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
    if ((DDL_COMP1_IsActiveFlag_IT(COMP2) != 0U) || (DDL_COMP1_ReadOutputLevel(COMP2) == 0U))
    {
        mask |= DRV_FAULT_PV_OCP;
    }
    return mask;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_comp_irq_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : 转发官方比较器IRQ处理，清除标志并累计事件。
 *---------------------------------------------------------------------------*/
void drv_comp_irq_ack(void)
{
    BSP_COMP0_IRQHandler();
    BSP_COMP2_IRQHandler();
}
