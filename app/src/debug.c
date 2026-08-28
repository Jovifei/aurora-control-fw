#include "debug.h"

/*---------------------------------------------------------------------------*
 * Name        : void aurora_debug_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 初始化应用Debug抽象。当前硬件Debug UART尚未纳入量产驱动，因此默认保持无输出。
 *---------------------------------------------------------------------------*/
void aurora_debug_init(void)
{
    /* 预留统一入口；AURORA_DEBUG_ENABLE默认关闭，不在此处占用产品UART。 */
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_debug_log(const char *message)
 * Input       : message - 以NUL结尾的日志文本，可为NULL
 * Output      : 无
 * Description : Debug统一文本入口。当前不绑定产品UART，避免日志破坏产品协议和控制实时性。
 *---------------------------------------------------------------------------*/
void aurora_debug_log(const char *message)
{
    (void)message;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_debug_log_buffer(const char *prefix,
 *               const void *data, size_t length)
 * Input       : prefix - 可选前缀；data - 数据地址；length - 数据长度
 * Output      : 无
 * Description : Debug统一二进制入口。当前仅保留接口契约，后续接入PB7/PB8调试串口时在Driver层实现。
 *---------------------------------------------------------------------------*/
void aurora_debug_log_buffer(const char *prefix, const void *data, size_t length)
{
    (void)prefix;
    (void)data;
    (void)length;
}
