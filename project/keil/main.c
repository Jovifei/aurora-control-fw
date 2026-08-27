#include "service.h"

#include "g32f031xx.h"

extern aurora_service_t g_aurora_service;

aurora_service_t g_aurora_service;

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
