#include "driver.h"

#include "g32f031_ddl_iwdt.h"

bool drv_watchdog_init(uint32_t timeout_ms)
{
    uint32_t reload;

    /* IWDT典型时钟按40kHz估算，分频64；实板需测量真实复位时间。 */
    reload = (timeout_ms * 40000UL) / (64UL * 1000UL);
    if (reload == 0U)
    {
        reload = 1U;
    }
    if (reload > 0x0FFFU)
    {
        reload = 0x0FFFU;
    }

    DDL_IWDT_EnableWriteAccess(IWDT);
    DDL_IWDT_SetPrescaler(IWDT, DDL_IWDT_PSC_64);
    DDL_IWDT_SetReloadCounter(IWDT, reload);
    {
        uint32_t wait = 100000U;
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

void drv_watchdog_feed(void)
{
    DDL_IWDT_ReloadCounter(IWDT);
}
