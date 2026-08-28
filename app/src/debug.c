#include "debug.h"

#include "driver.h"

#include <stdarg.h>
#include <stdio.h>

#if (DEBUG_ENABLE != 0U)
/* 单条日志格式化缓冲区，超出容量的日志整条丢弃。 */
#define DEBUG_FORMAT_BUFFER_SIZE                    (256U)
static char g_debug_buffer[DEBUG_FORMAT_BUFFER_SIZE];
#endif
static uint32_t g_debug_dropped;

/*---------------------------------------------------------------------------*
 * Name        : void debug_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 清零Debug格式化丢弃计数；UART硬件由driver统一初始化。
 *---------------------------------------------------------------------------*/
void debug_init(void)
{
    g_debug_dropped = 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void debug_printf_locked(const char *tag, const char *fmt, ...)
 * Input       : tag - 公司前缀和模块标签；fmt - printf格式；... - 格式参数
 * Output      : 无
 * Description : 在主循环中格式化完整日志并原子送入UART环形缓冲；不阻塞、不允许ISR调用。
 *---------------------------------------------------------------------------*/
void debug_printf_locked(const char *tag, const char *fmt, ...)
{
#if (DEBUG_ENABLE != 0U)
    va_list arguments;
    int prefix_length;
    int body_length;
    size_t used;
    size_t remaining;

    if ((tag == NULL) || (fmt == NULL))
    {
        g_debug_dropped++;
        return;
    }

    prefix_length = snprintf(g_debug_buffer,
                             sizeof(g_debug_buffer),
                             "%s ",
                             tag);
    if ((prefix_length < 0) ||
        ((size_t)prefix_length >= sizeof(g_debug_buffer)))
    {
        g_debug_dropped++;
        return;
    }

    used = (size_t)prefix_length;
    remaining = sizeof(g_debug_buffer) - used;
    va_start(arguments, fmt);
    body_length = vsnprintf(&g_debug_buffer[used], remaining, fmt, arguments);
    va_end(arguments);
    if ((body_length < 0) || ((size_t)body_length >= remaining))
    {
        g_debug_dropped++;
        return;
    }

    used += (size_t)body_length;
    if ((used + 2U) > sizeof(g_debug_buffer))
    {
        g_debug_dropped++;
        return;
    }
    g_debug_buffer[used++] = '\r';
    g_debug_buffer[used++] = '\n';
    if (!drv_uart_send((const uint8_t *)g_debug_buffer, used))
    {
        g_debug_dropped++;
    }
#else
    (void)tag;
    (void)fmt;
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t debug_dropped_count(void)
 * Input       : 无
 * Output      : 因格式化溢出或UART缓冲不足而丢弃的日志数量
 * Description : 返回Debug日志丢弃计数，供主循环诊断和Host测试读取。
 *---------------------------------------------------------------------------*/
uint32_t debug_dropped_count(void)
{
    return g_debug_dropped;
}
