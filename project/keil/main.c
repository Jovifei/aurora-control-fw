#include "service.h"

#include "driver.h"
#include "g32f031xx.h"

extern aurora_service_t g_aurora_service;

aurora_service_t g_aurora_service;

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 系统入口：先建立时基和最小功率GPIO安全态，再用PVD等待MCU VDD连续稳定；
 *               供电资格通过后才进入Service完整初始化（IWDT/PWM/COMP/ADC/UART/APP/Flash）。
 *               弱光等待不是故障，不触发软件锁存或PVD系统复位；初始化失败则保持安全等待。
 *---------------------------------------------------------------------------*/
int main(void)
{
    /*
     * v0.8.1弱光启动资格：在任何PWM/COMP/ADC/IWDT初始化之前，先把功率GPIO压到安全态。
     * drv_system_wait_for_supply_stable()可在弱光下无限等待，VDD每次跌破VPVD都会重新累计稳定时间。
     */
    drv_system_init();
    drv_io_init();
    drv_io_set_relay(false);
    drv_io_set_link(false);
    drv_io_set_leds(false, false);

    if (!drv_system_wait_for_supply_stable())
    {
        /* PVD模块自身建立异常：保持GLC/GHC低、继电器断开；此阶段IWDT尚未启动。 */
        for (;;)
        {
            __WFI();
        }
    }

    if (!aurora_service_init(&g_aurora_service))
    {
        /* 完整初始化失败保持安全态；看门狗若已启动会复位，否则停在这里等待调试。 */
        for (;;)
        {
            __WFI();
        }
    }

    for (;;)
    {
        aurora_service_poll(&g_aurora_service);
        __WFI();
    }
}
