#ifndef AURORA_DEBUG_H
#define AURORA_DEBUG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AURORA_DEBUG_ENABLE
#define AURORA_DEBUG_ENABLE                         (0U) /* Debug日志总开关；默认关闭。 */
#endif

/* Debug接口当前只提供统一入口，硬件Debug UART接入前默认不产生任何输出。 */
void aurora_debug_init(void);
void aurora_debug_log(const char *message);
void aurora_debug_log_buffer(const char *prefix, const void *data, size_t length);

#if AURORA_DEBUG_ENABLE != 0U
#define AURORA_DEBUG_LOG(message)                   aurora_debug_log(message) /* Debug文本入口。 */
#else
#define AURORA_DEBUG_LOG(message)                   ((void)(message)) /* Debug关闭时为空操作。 */
#endif

#ifdef __cplusplus
}
#endif

#endif
