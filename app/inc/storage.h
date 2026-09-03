#ifndef AURORA_STORAGE_H
#define AURORA_STORAGE_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* G32F031单个物理Flash擦除页大小，单位字节。 */
#define AURORA_STORAGE_PAGE_SIZE                    (512U)
/* Journal固定头部长度，包含Magic、版本、长度、序号、CRC和Commit Marker。 */
#define AURORA_STORAGE_HEADER_SIZE                  (24U)
/* Commit Marker在页内的字节偏移。 */
#define AURORA_STORAGE_COMMIT_OFFSET                (16U)
/* 最后写入的提交标记；未出现该值的页一律视为不完整。 */
#define AURORA_STORAGE_COMMIT_MARKER                (0xA55AA55AUL)
/* ASCII近似“AURA”的页头Magic。 */
#define AURORA_STORAGE_MAGIC                        (0x41555241UL)
/* v3保存双能量账本、余数、30min相位、运行模式和Demo参数。 */
#define AURORA_STORAGE_VERSION                      (3U)
/* v2保存单套PV能量和49点历史。 */
#define AURORA_STORAGE_VERSION_V2                   (2U)
/* v1只保存化学体系/电压档/总能量/旧daily/revision。 */
#define AURORA_STORAGE_VERSION_LEGACY               (1U)
#define AURORA_STORAGE_LEGACY_PAYLOAD_SIZE          (16U)
#define AURORA_STORAGE_V2_PAYLOAD_SIZE              \
    (16U + (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))
/* v3固定载荷：56字节元数据 + 两套49点uint32历史。 */
#define AURORA_STORAGE_PAYLOAD_SIZE                 \
    (56U + (AURORA_ENERGY_HISTORY_POINT_COUNT * 8U))

/* 页头字段偏移。 */
#define AURORA_STORAGE_MAGIC_OFFSET                 (0U)
#define AURORA_STORAGE_VERSION_OFFSET               (4U)
#define AURORA_STORAGE_LENGTH_OFFSET                (6U)
#define AURORA_STORAGE_SEQUENCE_OFFSET              (8U)
#define AURORA_STORAGE_CRC_OFFSET                   (12U)

/* v3载荷内部字段偏移。 */
#define AURORA_STORAGE_CHEMISTRY_OFFSET             (0U)
#define AURORA_STORAGE_PACK_OFFSET                  (1U)
#define AURORA_STORAGE_MODE_OFFSET                  (2U)
#define AURORA_STORAGE_HISTORY_COUNT_OFFSET         (3U)
#define AURORA_STORAGE_ENERGY_SEMANTICS_OFFSET      (4U)
#define AURORA_STORAGE_LIFETIME_ENERGY_OFFSET       (8U)
#define AURORA_STORAGE_DAILY_ENERGY_OFFSET          (12U)
#define AURORA_STORAGE_CHARGE_LIFETIME_OFFSET       (16U)
#define AURORA_STORAGE_CHARGE_DAILY_OFFSET          (20U)
#define AURORA_STORAGE_REVISION_OFFSET              (24U)
#define AURORA_STORAGE_HISTORY_ELAPSED_OFFSET       (28U)
#define AURORA_STORAGE_DEMO_VOLTAGE_OFFSET          (32U)
#define AURORA_STORAGE_DEMO_POWER_OFFSET            (36U)
#define AURORA_STORAGE_PV_REMAINDER_OFFSET          (40U)
#define AURORA_STORAGE_CHARGE_REMAINDER_OFFSET      (48U)
#define AURORA_STORAGE_ENERGY_HISTORY_OFFSET        (56U)
#define AURORA_STORAGE_CHARGE_HISTORY_OFFSET        \
    (AURORA_STORAGE_ENERGY_HISTORY_OFFSET + \
     (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))

/* v1/v2兼容字段偏移。 */
#define AURORA_STORAGE_V2_HISTORY_COUNT_OFFSET      (2U)
#define AURORA_STORAGE_V2_LIFETIME_OFFSET           (4U)
#define AURORA_STORAGE_V2_DAILY_OFFSET              (8U)
#define AURORA_STORAGE_V2_REVISION_OFFSET           (12U)
#define AURORA_STORAGE_V2_HISTORY_OFFSET            (16U)

/* Flash页分类结果；区分工厂擦除、半写、版本、CRC和内容错误。 */
typedef uint8_t aurora_storage_page_status_t;
#define AURORA_STORAGE_PAGE_VALID                   ((aurora_storage_page_status_t)0U) // 当前v3页有效。
#define AURORA_STORAGE_PAGE_VALID_LEGACY            ((aurora_storage_page_status_t)1U) // v1/v2旧页有效，需迁移到v3。
#define AURORA_STORAGE_PAGE_ERASED                  ((aurora_storage_page_status_t)2U) // 整页为擦除态，可作为空槽。
#define AURORA_STORAGE_PAGE_INCOMPLETE              ((aurora_storage_page_status_t)3U) // Commit Marker缺失，视为掉电半写。
#define AURORA_STORAGE_PAGE_VERSION_ERROR           ((aurora_storage_page_status_t)4U) // Magic有效但版本或长度不支持。
#define AURORA_STORAGE_PAGE_CRC_ERROR               ((aurora_storage_page_status_t)5U) // 载荷CRC不匹配。
#define AURORA_STORAGE_PAGE_CONTENT_ERROR           ((aurora_storage_page_status_t)6U) // CRC正确但字段范围或历史单调性错误。
#define AURORA_STORAGE_PAGE_IO_ERROR                ((aurora_storage_page_status_t)7U) // 底层Flash读取失败。

/* 当前可信Journal页；NONE表示尚无已提交的可信页。 */
typedef uint8_t aurora_storage_active_page_t;
/* 尚无已提交可信页。 */
#define AURORA_STORAGE_ACTIVE_NONE                  ((aurora_storage_active_page_t)0U)
/* A页是当前已提交可信页。 */
#define AURORA_STORAGE_ACTIVE_PAGE_A                ((aurora_storage_active_page_t)1U)
/* B页是当前已提交可信页。 */
#define AURORA_STORAGE_ACTIVE_PAGE_B                ((aurora_storage_active_page_t)2U)

/* Flash双页Journal运行状态。 */
typedef struct
{
    aurora_persistent_settings_t settings;           /* 当前已应用的持久化设置。 */
    uint32_t sequence;                               /* 最近有效页的单调递增序号。 */
    uint32_t dirty_since_ms;                         /* 首次变脏时间，用于合并多次写。 */
    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */
    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */
    bool write_inhibited;                            /* 连续安全供电I/O失败后禁止本会话继续擦写。 */
    aurora_storage_active_page_t active_page;        /* 当前仍可信的已提交页，写失败时不得切换。 */
    aurora_storage_page_status_t page_a_status;      /* 最近启动读取A页分类。 */
    aurora_storage_page_status_t page_b_status;      /* 最近启动读取B页分类。 */
} aurora_storage_ctx_t;

void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx);
void aurora_storage_mark_dirty(aurora_storage_ctx_t *ctx, uint32_t now_ms);
aurora_storage_page_status_t aurora_storage_classify_page(
    const uint8_t *page,
    size_t page_size,
    aurora_persistent_settings_t *settings,
    uint32_t *sequence);
bool aurora_storage_decode_page(const uint8_t *page,
                                size_t page_size,
                                aurora_persistent_settings_t *settings,
                                uint32_t *sequence);
size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
                                  uint8_t *page,
                                  size_t page_size,
                                  bool committed);
size_t aurora_storage_encode_page_sequence(const aurora_persistent_settings_t *settings,
                                           uint32_t sequence,
                                           uint8_t *page,
                                           size_t page_size,
                                           bool committed);
void aurora_storage_energy_history_update(aurora_persistent_settings_t *settings);
void aurora_storage_energy_history_checkpoint(aurora_persistent_settings_t *settings);
uint32_t aurora_storage_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
