#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_atmr.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_comp0.h"
#include "g32f031_ddl_comp1.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"

static volatile uint32_t g_staged_sequence;
static volatile uint32_t g_applied_sequence;

static uint32_t duty_to_compare(uint16_t duty_q15)
{
    uint32_t compare = ((uint32_t)duty_q15 * BOARD_PWM_PERIOD_COUNTS) /
                       DRV_DUTY_Q15_ONE;
    if (compare >= BOARD_PWM_PERIOD_COUNTS)
    {
        compare = BOARD_PWM_PERIOD_COUNTS - 1U;
    }
    return compare;
}

static void connect_glc_to_atmr(void)
{
    DDL_GPIO_InitTypeDef gpio = {0U};

    /* 此前PA15一直保持GPIO低；只有定时器、CCR=0、MOE=0均就绪后才接入复用。 */
    gpio.Pin = DDL_GPIO_PIN_15;
    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_DISABLE;
    gpio.Pull = DDL_GPIO_PULL_DOWN;
    gpio.Alternate = DDL_GPIO_AF_3;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_SetAF3Pin_10_15(GPIOA, DDL_GPIO_PIN_15, DDL_GPIO_AF3_ATMR_CH0);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);
}

bool drv_pwm_init(void)
{
    DDL_ATMR_InitTypeDef timer;
    DDL_ATMR_OC_InitTypeDef output;
    DDL_ATMR_BDT_InitTypeDef break_deadtime;

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_ATMR);
    DDL_RCC_Lock();

    DDL_ATMR_StructInit(&timer);
    timer.Prescaler = 0U;
    timer.CounterMode = DDL_ATMR_COUNTERMODE_UP;
    timer.Autoreload = BOARD_PWM_PERIOD_COUNTS - 1U;
    timer.ClockDivision = DDL_ATMR_CLOCKDIVISION_DIV1;
    timer.RepetitionCounter = 0U;
    if (DDL_ATMR_Init(ATMR, &timer) != SUCCESS)
    {
        return false;
    }
    DDL_ATMR_EnableARRPreload(ATMR);

    DDL_ATMR_OC_StructInit(&output);
    output.OCMode = DDL_ATMR_OCMODE_PWM1;
    output.OCState = ENABLE;
    output.OCNState = DISABLE;
    output.OCPolarity = DDL_ATMR_OCPOLARITY_HIGH;
    output.OCNPolarity = DDL_ATMR_OCPOLARITY_HIGH;
    output.OCIdleState = DDL_ATMR_OCIDLESTATE_LOW;
    output.OCNIdleState = DDL_ATMR_OCIDLESTATE_LOW;
    output.CompareValue = 0U;
    if (DDL_ATMR_OC_Init(ATMR, DDL_ATMR_CHANNEL_CH0, &output) != SUCCESS)
    {
        return false;
    }
    DDL_ATMR_OC_EnablePreload(ATMR, DDL_ATMR_CHANNEL_CH0);

    /* CH3只生成固定ADC采样触发点，不连接外部引脚。 */
    output.OCState = DISABLE;
    output.CompareValue = BOARD_PWM_PERIOD_COUNTS / 2U;
    (void)DDL_ATMR_OC_Init(ATMR, DDL_ATMR_CHANNEL_CH3, &output);
    DDL_ATMR_SetTriggerOutputMode0(ATMR, DDL_ATMR_TRGO_OC3REF);

    DDL_ATMR_BDT_StructInit(&break_deadtime);
    break_deadtime.BreakState = DDL_ATMR_BREAK_ENABLE;
    break_deadtime.BreakPolarity = DDL_ATMR_BREAK_POLARITY_LOW;
    break_deadtime.AutomaticOutput = DDL_ATMR_AUTOMATICOUTPUT_DISABLE;
    break_deadtime.DeadTime0 = 0U;
    break_deadtime.DeadTime1 = 0U;
    if (DDL_ATMR_BDT_Init(ATMR, &break_deadtime) != SUCCESS)
    {
        return false;
    }

    /* MOS瞬时过流COMP0作为硬件Break；COMP2通过最高优先级中断补充输入过流保护。 */
    DDL_ATMR_SetBreakSource(ATMR, DDL_ATMR_BREAKSOURCE_COMP0);
    DDL_ATMR_DisableAutomaticOutput(ATMR);
    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);

    /* 初始化阶段只允许这一次软件UEV，把0占空比装入活动寄存器。 */
    DDL_ATMR_GenerateEvent_UPDATE(ATMR);
    DDL_ATMR_ClearFlag_UPDATE(ATMR);
    DDL_ATMR_DisableIT_UPDATE(ATMR);
    DDL_ATMR_EnableIT_BRK(ATMR);

    g_staged_sequence = 0U;
    g_applied_sequence = 0U;

    connect_glc_to_atmr();
    DDL_ATMR_EnableCounter(ATMR);
    NVIC_EnableIRQ(ATMR_BRK_UP_TRG_COM_IRQn);
    return true;
}

void drv_pwm_force_off_isr(void)
{
    /* 只做恒定时间的硬关断；Break中断屏蔽由Break ISR单独负责。 */
    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
}

void drv_pwm_quiesce_break_irq_isr(void)
{
    /* Break标志保持锁存，只暂时禁止重复进入，防止持续故障造成中断风暴。 */
    DDL_ATMR_DisableIT_BRK(ATMR);
}

void drv_pwm_disarm(void)
{
    aurora_irq_state_t irq = drv_irq_save();
    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    drv_irq_restore(irq);
}

bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
{
    aurora_irq_state_t irq = drv_irq_save();

    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    DDL_ATMR_OC_SetCompareCH0(ATMR, 0U);
    g_staged_sequence++;
    DDL_ATMR_ClearFlag_UPDATE(ATMR);
    /* UPDATE与Break共用向量，仅为首次零占空比装载临时开一次中断。 */
    DDL_ATMR_EnableIT_UPDATE(ATMR);
    if (sequence != NULL)
    {
        *sequence = g_staged_sequence;
    }
    drv_irq_restore(irq);
    return true;
}

bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence)
{
    if (duty_q15 > BOARD_PWM_MAX_DUTY_Q15)
    {
        duty_q15 = BOARD_PWM_MAX_DUTY_Q15;
    }
    DDL_ATMR_OC_SetCompareCH0(ATMR, duty_to_compare(duty_q15));
    g_staged_sequence++;
    if (sequence != NULL)
    {
        *sequence = g_staged_sequence;
    }
    return true;
}

bool drv_pwm_arm(void)
{
    if (drv_pwm_break_source_active() || drv_pwm_break_latched())
    {
        return false;
    }

    DDL_ATMR_CC_EnableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    DDL_ATMR_EnableAllOutputs(ATMR);

    /* 防止检查与MOE写入之间发生故障：写入后必须立即复核。 */
    if (drv_pwm_break_source_active() || drv_pwm_break_latched())
    {
        DDL_ATMR_DisableAllOutputs(ATMR);
        DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
        return false;
    }
    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;
}

bool drv_pwm_output_active(void)
{
    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;
}

bool drv_pwm_break_source_active(void)
{
    return (DDL_COMP0_ReadOutputLevel(COMP0) == 0U) ||
           (DDL_COMP1_ReadOutputLevel(COMP2) != 0U);
}

bool drv_pwm_break_latched(void)
{
    return DDL_ATMR_IsActiveFlag_BRK(ATMR) != 0U;
}

bool drv_pwm_clear_break_latch(void)
{
    if (drv_pwm_output_active() || drv_pwm_break_source_active())
    {
        return false;
    }
    DDL_ATMR_ClearFlag_BRK(ATMR);
    DDL_ATMR_EnableIT_BRK(ATMR);
    return !drv_pwm_break_latched();
}

uint32_t drv_pwm_applied_sequence(void)
{
    return g_applied_sequence;
}

void drv_pwm_update_isr_ack(void)
{
    if (DDL_ATMR_IsActiveFlag_UPDATE(ATMR) != 0U)
    {
        DDL_ATMR_ClearFlag_UPDATE(ATMR);
        g_applied_sequence = g_staged_sequence;
        DDL_ATMR_DisableIT_UPDATE(ATMR);
    }
}
