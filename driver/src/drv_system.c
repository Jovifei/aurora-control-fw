#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_pmu.h"
#include "g32f031_ddl_rcc.h"
#include "g32f031xx.h"
#include "system_g32f031.h"

/* 微秒换算毫秒，供PVD Ready超时向1ms SysTick时基取整。 */
#define DRIVER_US_PER_MS (1000UL)

/* 当前启动方案明确禁止PVD成为系统复位源或运行期中断保护源。 */
#if BOARD_MCU_PVD_RESET_ENABLE != 0U
#error "BOARD_MCU_PVD_RESET_ENABLE must remain 0: PVD is a boot qualifier, not a reset source"
#endif
#if BOARD_MCU_PVD_IRQ_ENABLE != 0U
#error "BOARD_MCU_PVD_IRQ_ENABLE must remain 0: weak-light VDD is not a latched runtime fault"
#endif

/* 把board中的名义mV参数映射为G32F031硬件PVD档位。 */
#if BOARD_MCU_PVD_THRESHOLD_MV == 2000UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_1
#elif BOARD_MCU_PVD_THRESHOLD_MV == 2400UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_2
#elif BOARD_MCU_PVD_THRESHOLD_MV == 2800UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_3
#elif BOARD_MCU_PVD_THRESHOLD_MV == 3200UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_4
#elif BOARD_MCU_PVD_THRESHOLD_MV == 3600UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_5
#elif BOARD_MCU_PVD_THRESHOLD_MV == 4000UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_6
#elif BOARD_MCU_PVD_THRESHOLD_MV == 4400UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_7
#elif BOARD_MCU_PVD_THRESHOLD_MV == 4800UL
#define DRIVER_PVD_THRESHOLD DDL_PMU_PVD_THRESHOLD_8
#else
#error "Unsupported BOARD_MCU_PVD_THRESHOLD_MV"
#endif

/* PVD滤波长度以复位后的64MHz HSI系统时钟为依据。 */
#if BOARD_MCU_PVD_FILTER_US == 1UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_64
#elif BOARD_MCU_PVD_FILTER_US == 2UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_128
#elif BOARD_MCU_PVD_FILTER_US == 3UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_192
#elif BOARD_MCU_PVD_FILTER_US == 5UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_320
#elif BOARD_MCU_PVD_FILTER_US == 10UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_640
#elif BOARD_MCU_PVD_FILTER_US == 20UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_1280
#elif BOARD_MCU_PVD_FILTER_US == 30UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_1920
#elif BOARD_MCU_PVD_FILTER_US == 50UL
#define DRIVER_PVD_FILTER_LENGTH DDL_PMU_PVD_FILTER_LENGTH_3200
#else
#error "Unsupported BOARD_MCU_PVD_FILTER_US"
#endif

static volatile uint32_t g_system_ms;

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t pvd_ready_timeout_ms(void)
 * Input       : 无
 * Output      : 按1ms SysTick向上取整后的PVD Ready超时
 * Description : BOARD参数用us表达硬件建立时间，这里只做时基换算；至少保留1ms观察窗口。
 *---------------------------------------------------------------------------*/
static uint32_t pvd_ready_timeout_ms(void)
{
    uint32_t timeout_ms =
        (BOARD_MCU_PVD_READY_TIMEOUT_US + DRIVER_US_PER_MS - 1UL) / DRIVER_US_PER_MS;

    if (timeout_ms == 0U)
    {
        timeout_ms = 1U;
    }
    return timeout_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 更新系统时钟、清零毫秒计数并配置1 ms SysTick；复位后系统仍使用默认稳定HSI。
 *---------------------------------------------------------------------------*/
void drv_system_init(void)
{
    SystemCoreClockUpdate();
    g_system_ms = 0U;
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_time_now_ms(void)
 * Input       : 无
 * Output      : 系统毫秒时间戳
 * Description : 返回由SysTick维护的系统毫秒计数。
 *---------------------------------------------------------------------------*/
uint32_t drv_time_now_ms(void)
{
    return g_system_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_time_tick_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 在SysTick ISR中递增系统毫秒计数。
 *---------------------------------------------------------------------------*/
void drv_time_tick_isr(void)
{
    g_system_ms++;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_system_supply_qualifier_init(void)
 * Input       : 无
 * Output      : true表示HSI已就绪且PVD已按启动资格模式完成配置
 * Description : 先确认复位默认HSI已稳定，再配置VPVD门限和数字滤波；明确关闭PVD中断与PVD系统复位，
 *               PVD仅用于上电资格判断。
 *---------------------------------------------------------------------------*/
bool drv_system_supply_qualifier_init(void)
{
    /* 手册规定HSIRDY表示HSI已稳定；PVD滤波按64MHz HSI计算，因此先确认该前提。 */
    if (DDL_RCC_HSI_IsReady() == 0U)
    {
        return false;
    }

    /* 先关闭PVD系统复位，防止弱光在门限附近形成“复位→重启→再复位”的循环。 */
    DDL_RCC_Unlock();
    DDL_RCC_Disable_PVDRST();
    DDL_RCC_Lock();

    DDL_PMU_Unlock();
    DDL_PMU_DisablePVD();
    DDL_PMU_DisableIT_PVD();
    DDL_PMU_DisablePVDLT();
    DDL_PMU_DisablePVDHT();
    DDL_PMU_DisableFilter();
    DDL_PMU_SetPVDVoltageThreshold(DRIVER_PVD_THRESHOLD);
    DDL_PMU_SetPVDFilterLength(DRIVER_PVD_FILTER_LENGTH);
    DDL_PMU_ClearFlag_PVDF();
    DDL_PMU_EnableFilter();
    DDL_PMU_EnablePVD();
    DDL_PMU_Lock();

    /* 配置结果必须满足“PVD开、IRQ关、Reset关”三个硬约束。 */
    return (DDL_PMU_IsEnabledPVD() != 0U) && (DDL_PMU_IsEnabledIT_PVD() == 0U) &&
           (DDL_RCC_IsEnabled_PVDRST() == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_system_supply_monitor_ready(void)
 * Input       : 无
 * Output      : true表示PVD已经越过内部建立时间，可相信PVDSTS
 * Description : 直接读取PVDRDY；PVD刚使能时约30us内该标志保持0。
 *---------------------------------------------------------------------------*/
bool drv_system_supply_monitor_ready(void)
{
    return DDL_PMU_IsActiveFlag_PVDRDY() != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_system_supply_is_good(void)
 * Input       : 无
 * Output      : true表示当前VDD高于所选VPVD门限
 * Description : 只在PVDRDY有效后调用；低于门限只表示“继续等待”，不产生软件故障。
 *---------------------------------------------------------------------------*/
bool drv_system_supply_is_good(void)
{
    return DDL_PMU_GetPVDMonitoringResult() == DDL_PMU_VDD_ABOVE_PVD_OUTPUT;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_supply_qualifier_stop(void)
 * Input       : 无
 * Output      : 无
 * Description : 供电资格通过或PVD模块异常后关闭PVD；正常运行阶段不使用PVD做弱光保护。
 *---------------------------------------------------------------------------*/
void drv_system_supply_qualifier_stop(void)
{
    DDL_PMU_Unlock();
    DDL_PMU_DisablePVD();
    DDL_PMU_DisableFilter();
    DDL_PMU_DisableIT_PVD();
    DDL_PMU_ClearFlag_PVDF();
    DDL_PMU_Lock();
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_system_wait_for_supply_stable(void)
 * Input       : 无
 * Output      : true表示VDD连续稳定满足配置时间；false仅表示PVD/时钟模块自身建立异常
 * Description : 弱光下可无限等待；每次VDD跌破VPVD都把连续稳定计时清零。等待期间只靠SysTick唤醒，
 *               不启动IWDT，也不初始化PWM/COMP/ADC/UART/Flash业务，更不会生成软件Fault。
 *---------------------------------------------------------------------------*/
bool drv_system_wait_for_supply_stable(void)
{
    const uint32_t ready_timeout_ms = pvd_ready_timeout_ms();
    uint32_t ready_start_ms;
    uint32_t stable_start_ms = 0U;
    uint32_t last_check_ms = 0U;
    bool stable_tracking = false;

    if (!drv_system_supply_qualifier_init())
    {
        return false;
    }

    ready_start_ms = drv_time_now_ms();
    while (!drv_system_supply_monitor_ready())
    {
        if ((drv_time_now_ms() - ready_start_ms) >= ready_timeout_ms)
        {
            drv_system_supply_qualifier_stop();
            return false;
        }
        __WFI();
    }

    last_check_ms = drv_time_now_ms();
    for (;;)
    {
        uint32_t now_ms = drv_time_now_ms();

        if ((now_ms - last_check_ms) < BOARD_MCU_SUPPLY_CHECK_PERIOD_MS)
        {
            __WFI();
            continue;
        }
        last_check_ms = now_ms;

        if (!drv_system_supply_is_good())
        {
            /* 弱光或辅助电源抖动：不是故障，只撤销本轮连续稳定计时。 */
            stable_tracking = false;
            stable_start_ms = 0U;
            __WFI();
            continue;
        }

#if BOARD_MCU_SUPPLY_STABLE_TIME_MS == 0U
        break;
#else
        if (!stable_tracking)
        {
            stable_tracking = true;
            stable_start_ms = now_ms;
        }
        else if ((now_ms - stable_start_ms) >= BOARD_MCU_SUPPLY_STABLE_TIME_MS)
        {
            break;
        }
#endif

        __WFI();
    }

    drv_system_supply_qualifier_stop();
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_irq_state_t drv_irq_save(void)
 * Input       : 无
 * Output      : 调用前的PRIMASK状态
 * Description : 保存PRIMASK并关闭全局中断，供极短临界区使用。
 *---------------------------------------------------------------------------*/
aurora_irq_state_t drv_irq_save(void)
{
    aurora_irq_state_t state = __get_PRIMASK();
    __disable_irq();
    return state;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_restore(aurora_irq_state_t state)
 * Input       : state - 目标状态
 * Output      : 无
 * Description : 恢复调用前的PRIMASK中断状态。
 *---------------------------------------------------------------------------*/
void drv_irq_restore(aurora_irq_state_t state)
{
    __set_PRIMASK(state);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_configure_priorities(void)
 * Input       : 无
 * Output      : 无
 * Description : 按快速故障、DMA、SysTick、UART顺序配置NVIC优先级。
 *---------------------------------------------------------------------------*/
void drv_irq_configure_priorities(void)
{
    /* 数值越小优先级越高：快速故障必须抢占PWM更新、DMA和通信。 */
    NVIC_SetPriority(COMP0_IRQn, 0U);
    NVIC_SetPriority(COMP1_2_3_IRQn, 0U);
    NVIC_SetPriority(ATMR_BRK_UP_TRG_COM_IRQn, 0U);
    NVIC_SetPriority(DMA_CH1_IRQn, 1U);
    NVIC_SetPriority(ADC_IRQn, 2U);
    NVIC_SetPriority(SysTick_IRQn, 2U);
    NVIC_SetPriority(USART_IRQn, 3U);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_reset(void)
 * Input       : 无
 * Output      : 无
 * Description : 请求Cortex-M系统复位。
 *---------------------------------------------------------------------------*/
DRV_SYSTEM_NORETURN void drv_system_reset(void)
{
    NVIC_SystemReset();
}
