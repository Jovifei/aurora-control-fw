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
/* 当前持久化载荷格式版本；v2增加49点24h累计能量历史。 */
#define AURORA_STORAGE_VERSION                      (2U)
/* v1仅保存化学体系/电压档/总能量/旧daily/revision，继续允许安全迁移。 */
#define AURORA_STORAGE_VERSION_LEGACY               (1U)
#define AURORA_STORAGE_LEGACY_PAYLOAD_SIZE          (16U)
/* v2前16字节保持旧字段位置，随后追加49个uint32累计Wh快照。 */
#define AURORA_STORAGE_PAYLOAD_SIZE                 \
    (16U + (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))

/* 页头字段偏移。 */
#define AURORA_STORAGE_MAGIC_OFFSET                 (0U)
#define AURORA_STORAGE_VERSION_OFFSET               (4U)
#define AURORA_STORAGE_LENGTH_OFFSET                (6U)
#define AURORA_STORAGE_SEQUENCE_OFFSET              (8U)
#define AURORA_STORAGE_CRC_OFFSET                   (12U)

/* 设置载荷内部字段偏移。 */
#define AURORA_STORAGE_CHEMISTRY_OFFSET             (0U)
#define AURORA_STORAGE_PACK_OFFSET                  (1U)
#define AURORA_STORAGE_HISTORY_COUNT_OFFSET         (2U)
#define AURORA_STORAGE_LIFETIME_ENERGY_OFFSET       (4U)
#define AURORA_STORAGE_DAILY_ENERGY_OFFSET          (8U)
#define AURORA_STORAGE_REVISION_OFFSET              (12U)
#define AURORA_STORAGE_ENERGY_HISTORY_OFFSET        (16U)

/* Flash页分类结果；区分工厂擦除、半写、版本、CRC和内容错误。 */
typedef uint8_t aurora_storage_page_status_t;
/* 页状态编码供启动分类、自愈策略和诊断使用。 */
#define AURORA_STORAGE_PAGE_VALID                   ((aurora_storage_page_status_t)0U)
#define AURORA_STORAGE_PAGE_VALID_LEGACY            ((aurora_storage_page_status_t)1U)
#define AURORA_STORAGE_PAGE_ERASED                  ((aurora_storage_page_status_t)2U)
#define AURORA_STORAGE_PAGE_INCOMPLETE              ((aurora_storage_page_status_t)3U)
#define AURORA_STORAGE_PAGE_VERSION_ERROR           ((aurora_storage_page_status_t)4U)
#define AURORA_STORAGE_PAGE_CRC_ERROR               ((aurora_storage_page_status_t)5U)
#define AURORA_STORAGE_PAGE_CONTENT_ERROR           ((aurora_storage_page_status_t)6U)
#define AURORA_STORAGE_PAGE_IO_ERROR                ((aurora_storage_page_status_t)7U)

/* Flash双页Journal运行状态。 */
typedef struct
{
    aurora_persistent_settings_t settings;           /* 当前已应用的持久化设置。 */
    uint32_t sequence;                               /* 最近有效页的单调递增序号。 */
    uint32_t dirty_since_ms;                         /* 首次变脏时间，用于合并多次写。 */
    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */
    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */
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
void aurora_storage_energy_history_update(aurora_persistent_settings_t *settings);
void aurora_storage_energy_history_checkpoint(aurora_persistent_settings_t *settings);
uint32_t aurora_storage_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
