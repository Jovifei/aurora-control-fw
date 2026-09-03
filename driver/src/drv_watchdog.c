#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_iwdt.h"

/*---------------------------------------------------------------------------*
 * Name        : bool drv_watchdog_init(uint32_t timeout_ms)
 * Input       : timeout_ms - 超时时间，ms
 * Output      : true表示IWDT重载值已生效并启动；false表示参数更新超时
 * Description : 按目标超时时间计算IWDT重载值，配置分频并等待寄存器更新完成后启动看门狗。
 *---------------------------------------------------------------------------*/
bool drv_watchdog_init(uint32_t timeout_ms)
{
    uint32_t reload;

    /* IWDT按数据手册典型32.768kHz估算、分频64；实板仍需测量LSI容差下的真实复位时间。 */
    reload = (timeout_ms * BOARD_WATCHDOG_CLOCK_HZ) /
             (BOARD_WATCHDOG_PRESCALER * BOARD_MILLISECONDS_PER_SECOND);
    if (reload == 0U)
    {
        reload = 1U;
    }
    if (reload > BOARD_WATCHDOG_RELOAD_MAX)
    {
        reload = BOARD_WATCHDOG_RELOAD_MAX;
    }

    DDL_IWDT_EnableWriteAccess(IWDT);
    DDL_IWDT_SetPrescaler(IWDT, DDL_IWDT_PSC_64);
    DDL_IWDT_SetReloadCounter(IWDT, reload);
    {
        uint32_t wait = BOARD_WATCHDOG_READY_TIMEOUT_LOOPS;
        while (!DDL_IWDT_IsReady(IWDT) && (wait > 0U))
        {
            wait--;
        }
        if (wait == 0U)
        {
            return false;
        }
    }
    DDL_IWDT_ReloadCounter(IWDT);
    DDL_IWDT_Enable(IWDT);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_watchdog_feed(void)
 * Input       : 无
 * Output      : 无
 * Description : 重装IWDT计数器；生产代码只允许Service健康监督调用。
 *---------------------------------------------------------------------------*/
void drv_watchdog_feed(void)
{
    DDL_IWDT_ReloadCounter(IWDT);
}
