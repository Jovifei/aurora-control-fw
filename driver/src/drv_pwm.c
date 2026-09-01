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

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t duty_to_compare(uint16_t duty_q15)
 * Input       : duty_q15 - Q15物理占空比
 * Output      : 与Q15物理占空比对应的ATMR比较值
 * Description : 把Q15物理占空比转换为ATMR CH0比较值，并限制在有效计数范围内。
 *---------------------------------------------------------------------------*/
static uint32_t duty_to_compare(uint16_t duty_q15)
{
    uint32_t compare = ((uint32_t)duty_q15 * BOARD_PWM_PERIOD_COUNTS) / DRV_DUTY_Q15_ONE;
    if (compare >= BOARD_PWM_PERIOD_COUNTS)
    {
        compare = BOARD_PWM_PERIOD_COUNTS - 1U;
    }
    return compare;
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_Init(void)
 * Input       : 无
 * Output      : 无
 * Description : 配置PA15/ATMR_CH0为GLC发波路径、COMP0低有效Break。
 *               原理图为二极管Boost，不使能PA14/CH0N。默认0 Duty、关闭MOE和AO。
 *---------------------------------------------------------------------------*/
void BSP_PWM_Init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0};
    DDL_ATMR_InitTypeDef tim = {0};
    DDL_ATMR_OC_InitTypeDef oc = {0};
    DDL_ATMR_BDT_InitTypeDef bdt = {0};

    /* 时钟使能 */
    DDL_RCC_Unlock();
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_ATMR);
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA);
    DDL_RCC_Lock();

    /* GPIO：仅PA15=CH0/GLC；PA14/GHC保持drv_io的GPIO低 */
    gpio.Pin = DDL_GPIO_PIN_15;
    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_3;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_SetAF3Pin_10_15(GPIOA, DDL_GPIO_PIN_15, DDL_GPIO_AF3_ATMR_CH0);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);

    /* 时基：50kHz载波 */
    tim.Prescaler = 0U;
    tim.CounterMode = DDL_ATMR_COUNTERMODE_UP;
    tim.Autoreload = BOARD_PWM_PERIOD_COUNTS - 1U;
    tim.ClockDivision = DDL_ATMR_CLOCKDIVISION_DIV1;
    tim.RepetitionCounter = 0U;
    (void)DDL_ATMR_Init(ATMR, &tim);
    DDL_ATMR_EnableARRPreload(ATMR);

    /* 单路PWM1，初始比较值为0 */
    oc.OCMode = DDL_ATMR_OCMODE_PWM1;
    oc.OCState = DDL_ATMR_OCSTATE_ENABLE;
    oc.OCNState = DDL_ATMR_OCSTATE_DISABLE;
    oc.CompareValue = 0U;
    oc.OCPolarity = DDL_ATMR_OCPOLARITY_HIGH;
    oc.OCNPolarity = DDL_ATMR_OCPOLARITY_HIGH;
    oc.OCIdleState = DDL_ATMR_OCIDLESTATE_LOW;
    oc.OCNIdleState = DDL_ATMR_OCIDLESTATE_LOW;
    (void)DDL_ATMR_OC_Init(ATMR, DDL_ATMR_CHANNEL_CH0, &oc);
    DDL_ATMR_OC_EnablePreload(ATMR, DDL_ATMR_CHANNEL_CH0);

    /* COMP0 Break低有效；Automatic Output关闭 */
    DDL_ATMR_BDT_StructInit(&bdt);
    bdt.DeadTime0 = BOARD_PWM_DEADTIME_TICKS;
    bdt.DeadTime1 = BOARD_PWM_DEADTIME_TICKS;
    bdt.BreakState = DDL_ATMR_BREAK_ENABLE;
    bdt.BreakPolarity = DDL_ATMR_BREAK_POLARITY_LOW;
    bdt.AutomaticOutput = DDL_ATMR_AUTOMATICOUTPUT_DISABLE;
    (void)DDL_ATMR_BDT_Init(ATMR, &bdt);
    DDL_ATMR_SetBreakSource(ATMR, DDL_ATMR_BREAKSOURCE_COMP0);
    DDL_ATMR_DisableAutomaticOutput(ATMR);
    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0N);
    DDL_ATMR_ClearFlag_BRK(ATMR);

    /* 初始化阶段只允许这一次软件UEV，把0占空比装入活动寄存器。 */
    DDL_ATMR_GenerateEvent_UPDATE(ATMR);
    DDL_ATMR_ClearFlag_UPDATE(ATMR);
    DDL_ATMR_DisableIT_UPDATE(ATMR);
    DDL_ATMR_EnableIT_BRK(ATMR);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_Start(void)
 * Input       : 无
 * Output      : 无
 * Description : 启动ATMR计数并打开MOE；调用前必须已装载0 Duty且Break源无效。
 *---------------------------------------------------------------------------*/
void BSP_PWM_Start(void)
{
    DDL_ATMR_EnableCounter(ATMR);
    DDL_ATMR_CC_EnableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    DDL_ATMR_EnableAllOutputs(ATMR);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_Stop(void)
 * Input       : 无
 * Output      : 无
 * Description : 关闭MOE与互补通道。计数器保持运行，供零CCR自然UEV握手使用。
 *---------------------------------------------------------------------------*/
void BSP_PWM_Stop(void)
{
    DDL_ATMR_DisableAllOutputs(ATMR);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0);
    DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0N);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_SetDuty(uint16_t permille)
 * Input       : permille - 占空比千分比（0~1000）
 * Output      : 无
 * Description : 按当前ARR周期设置CH0比较值，超过1000则限幅；写入shadow等待自然UEV。
 *---------------------------------------------------------------------------*/
void BSP_PWM_SetDuty(uint16_t permille)
{
    uint32_t period = DDL_ATMR_GetAutoReload(ATMR) + 1U;

    if (permille > 1000U)
    {
        permille = 1000U;
    }
    DDL_ATMR_OC_SetCompareCH0(ATMR, ((uint32_t)permille * period) / 1000U);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_SetFrequency(uint32_t frequency_hz)
 * Input       : frequency_hz - 目标PWM频率（Hz），0或超过SYSCLK则忽略
 * Output      : 无
 * Description : 修改ARR并保持当前占空比比例不变。
 *---------------------------------------------------------------------------*/
void BSP_PWM_SetFrequency(uint32_t frequency_hz)
{
    uint32_t old_period;
    uint32_t old_compare;
    uint32_t new_period;

    if ((frequency_hz == 0U) || (frequency_hz > BOARD_PWM_TIMER_CLOCK_HZ))
    {
        return;
    }
    old_period = DDL_ATMR_GetAutoReload(ATMR) + 1U;
    old_compare = DDL_ATMR_OC_GetCompareCH0(ATMR);
    new_period = BOARD_PWM_TIMER_CLOCK_HZ / frequency_hz;
    DDL_ATMR_SetAutoReload(ATMR, new_period - 1U);
    DDL_ATMR_OC_SetCompareCH0(ATMR, (old_compare * new_period) / old_period);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_PWM_SetComplementary(uint32_t enable)
 * Input       : enable - 非0使能CH0N，0仅CH0
 * Output      : 无
 * Description : 控制互补通道CH0N输出使能。
 *---------------------------------------------------------------------------*/
void BSP_PWM_SetComplementary(uint32_t enable)
{
    if (enable != 0U)
    {
        DDL_ATMR_CC_EnableChannel(ATMR, DDL_ATMR_CHANNEL_CH0N);
    }
    else
    {
        DDL_ATMR_CC_DisableChannel(ATMR, DDL_ATMR_CHANNEL_CH0N);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_PWM_IsBraked(void)
 * Input       : 无
 * Output      : 非0表示ATMR Break标志已置位
 * Description : 读取ATMR Break锁存标志，与官方Application同名接口一致。
 *---------------------------------------------------------------------------*/
uint32_t BSP_PWM_IsBraked(void)
{
    return DDL_ATMR_IsActiveFlag_BRK(ATMR);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_init(void)
 * Input       : 无
 * Output      : true表示ATMR、preload和Break初始化成功
 * Description : 调用官方BSP_PWM_Init，再启动计数器并打开Break向量；默认MOE关闭。
 *---------------------------------------------------------------------------*/
bool drv_pwm_init(void)
{
    g_staged_sequence = 0U;
    g_applied_sequence = 0U;
    BSP_PWM_Init();
    DDL_ATMR_EnableCounter(ATMR);
    NVIC_EnableIRQ(ATMR_BRK_UP_TRG_COM_IRQn);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_force_off_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 在快速故障ISR中以恒定时间关闭MOE和互补通道，不做复杂清理。
 *---------------------------------------------------------------------------*/
void drv_pwm_force_off_isr(void)
{
    BSP_PWM_Stop();
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_quiesce_break_irq_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 屏蔽持续Break造成的重复中断，同时保留Break标志锁存供主循环确认。
 *---------------------------------------------------------------------------*/
void drv_pwm_quiesce_break_irq_isr(void)
{
    DDL_ATMR_DisableIT_BRK(ATMR);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_disarm(void)
 * Input       : 无
 * Output      : 无
 * Description : 在短临界区内关闭主输出和PWM通道，使普通控制链失去发波能力。
 *---------------------------------------------------------------------------*/
void drv_pwm_disarm(void)
{
    aurora_irq_state_t irq = drv_irq_save();
    BSP_PWM_Stop();
    drv_irq_restore(irq);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
 * Input       : sequence - 提交或记录序号输出
 * Output      : true表示零占空比提交已暂存；sequence返回待确认序号
 * Description : 关波后把零占空比写入preload并临时开启一次UPDATE中断，返回待确认的提交序号。
 *---------------------------------------------------------------------------*/
bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
{
    aurora_irq_state_t irq = drv_irq_save();

    BSP_PWM_Stop();
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

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence)
 * Input       : duty_q15 - Q15物理占空比；sequence - 提交或记录序号输出
 * Output      : true表示占空比已写入shadow；sequence返回提交序号
 * Description : 限幅并写入CCR shadow，递增暂存序号；运行期不产生软件UPDATE事件。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_arm(void)
 * Input       : 无
 * Output      : true表示PWM已安全放行，false表示Break或复核失败
 * Description : 在Break源和锁存均清除时启用互补通道与MOE，并在写入后立即复核故障。
 *---------------------------------------------------------------------------*/
bool drv_pwm_arm(void)
{
    if (drv_pwm_break_source_active() || drv_pwm_break_latched())
    {
        return false;
    }

    BSP_PWM_Start();

    if (drv_pwm_break_source_active() || drv_pwm_break_latched())
    {
        BSP_PWM_Stop();
        return false;
    }
    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_output_active(void)
 * Input       : 无
 * Output      : true表示MOE当前处于输出状态
 * Description : 读取ATMR主输出是否处于使能状态。
 *---------------------------------------------------------------------------*/
bool drv_pwm_output_active(void)
{
    return DDL_ATMR_IsEnabledAllOutputs(ATMR) != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_break_source_active(void)
 * Input       : 无
 * Output      : true表示外部Break源当前仍处于有效电平
 * Description : MOS/PV过流比较器均为低有效：电流接负端，过流时输出拉低。
 *---------------------------------------------------------------------------*/
bool drv_pwm_break_source_active(void)
{
    return (DDL_COMP0_ReadOutputLevel(COMP0) == 0U) || (DDL_COMP1_ReadOutputLevel(COMP2) == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_break_latched(void)
 * Input       : 无
 * Output      : true表示ATMR已锁存Break事件
 * Description : 读取ATMR Break锁存标志。
 *---------------------------------------------------------------------------*/
bool drv_pwm_break_latched(void)
{
    return BSP_PWM_IsBraked() != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_clear_break_latch(void)
 * Input       : 无
 * Output      : true表示锁存已清除，false表示条件不满足或仍有Break
 * Description : 仅在输出关闭且硬件故障源失效时清除Break锁存并恢复Break中断。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_pwm_applied_sequence(void)
 * Input       : 无
 * Output      : 最近一次已由UEV确认的提交序号
 * Description : 返回最近一次由自然UEV确认生效的CCR提交序号。
 *---------------------------------------------------------------------------*/
uint32_t drv_pwm_applied_sequence(void)
{
    return g_applied_sequence;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_update_isr_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : 确认一次性UPDATE事件，把暂存序号发布为已生效序号并立即关闭UPDATE中断。
 *---------------------------------------------------------------------------*/
void drv_pwm_update_isr_ack(void)
{
    if (DDL_ATMR_IsActiveFlag_UPDATE(ATMR) != 0U)
    {
        DDL_ATMR_ClearFlag_UPDATE(ATMR);
        g_applied_sequence = g_staged_sequence;
        DDL_ATMR_DisableIT_UPDATE(ATMR);
    }
}
