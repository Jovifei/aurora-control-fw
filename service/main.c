#include "service.h"

#include "g32f031xx.h"

/* 目标启动入口的公开原型，满足ARMClang缺少原型检查。 */
int main(void);

extern aurora_service_t g_aurora_service;

aurora_service_t g_aurora_service;

/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 系统入口：由Service依次建立时钟、GPIO安全态、IWDT、PWM关断、COMP/ADC/UART、应用和Flash；
 *               初始化失败时保持安全等待，成功后循环领取事件并用WFI等待下一次中断。
 *---------------------------------------------------------------------------*/
int main(void)
{
    if (!aurora_service_init(&g_aurora_service))
    {
        /* 初始化失败保持安全态；看门狗若已启动会复位，否则停在这里等待调试。 */
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
