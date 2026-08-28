#ifndef AURORA_DEBUG_H
#define AURORA_DEBUG_H

#include <stdint.h>

/* 全局Debug打印开关：默认关闭，避免蓝牙产品构建混入日志。 */
#ifndef DEBUG_ENABLE
#define DEBUG_ENABLE                              (0U)
#endif
/* 公司日志统一前缀。 */
#ifndef DEBUG_PRODUCT
#define DEBUG_PRODUCT                             "[GE_DEBUG]"
#endif
/* 系统启动和模式选择日志开关。 */
#ifndef DEBUG_SYSTEM_ENABLE
#define DEBUG_SYSTEM_ENABLE                       (1U)
#endif
/* 蓝牙设置和共享USART日志开关。 */
#ifndef DEBUG_BLE_ENABLE
#define DEBUG_BLE_ENABLE                          (1U)
#endif
/* MOS NTC换算和传感器状态日志开关。 */
#ifndef DEBUG_NTC_ENABLE
#define DEBUG_NTC_ENABLE                          (1U)
#endif
/* 软件保护锁存和恢复日志开关。 */
#ifndef DEBUG_PROTECTION_ENABLE
#define DEBUG_PROTECTION_ENABLE                   (1U)
#endif
/* Flash设置保存日志开关。 */
#ifndef DEBUG_STORAGE_ENABLE
#define DEBUG_STORAGE_ENABLE                      (1U)
#endif
/* 看门狗健康监督日志开关。 */
#ifndef DEBUG_WDG_ENABLE
#define DEBUG_WDG_ENABLE                          (1U)
#endif

#include "board_config.h"

#if (DEBUG_ENABLE != 0U) && (BOARD_USART_MODE != BOARD_USART_MODE_DEBUG)
#error "DEBUG_ENABLE requires BOARD_USART_MODE_DEBUG"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void debug_init(void);
void debug_printf_locked(const char *tag, const char *fmt, ...);
uint32_t debug_dropped_count(void);

#ifdef __cplusplus
}
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_SYSTEM_ENABLE != 0U)
/* 系统日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_SYSTEM_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[SYSTEM]", __VA_ARGS__); } while (0)
#else
/* 系统日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_SYSTEM_PRINTF(...) do { } while (0)
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_BLE_ENABLE != 0U)
/* 蓝牙日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_BLE_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[BLE]", __VA_ARGS__); } while (0)
#else
/* 蓝牙日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_BLE_PRINTF(...) do { } while (0)
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_NTC_ENABLE != 0U)
/* NTC日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_NTC_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[NTC]", __VA_ARGS__); } while (0)
#else
/* NTC日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_NTC_PRINTF(...) do { } while (0)
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_PROTECTION_ENABLE != 0U)
/* 保护日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_PROTECTION_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[PROTECTION]", __VA_ARGS__); } while (0)
#else
/* 保护日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_PROTECTION_PRINTF(...) do { } while (0)
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_STORAGE_ENABLE != 0U)
/* 存储日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_STORAGE_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[STORAGE]", __VA_ARGS__); } while (0)
#else
/* 存储日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_STORAGE_PRINTF(...) do { } while (0)
#endif

#if (DEBUG_ENABLE != 0U) && (DEBUG_WDG_ENABLE != 0U)
/* 看门狗日志宏：把格式串和参数统一送入GE_DEBUG前缀。 */
#define DEBUG_WDG_PRINTF(...) \
    do { debug_printf_locked(DEBUG_PRODUCT "[WDG]", __VA_ARGS__); } while (0)
#else
/* 看门狗日志关闭时不评价调用参数，避免产品构建引入副作用。 */
#define DEBUG_WDG_PRINTF(...) do { } while (0)
#endif

#endif
