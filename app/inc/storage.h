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
/* 当前持久化载荷格式版本。 */
#define AURORA_STORAGE_VERSION                      (1U)
/* 当前设置载荷长度，单位字节。 */
#define AURORA_STORAGE_PAYLOAD_SIZE                 (16U)

/* 页头字段偏移。 */
#define AURORA_STORAGE_MAGIC_OFFSET                 (0U)
#define AURORA_STORAGE_VERSION_OFFSET               (4U)
#define AURORA_STORAGE_LENGTH_OFFSET                (6U)
#define AURORA_STORAGE_SEQUENCE_OFFSET              (8U)
#define AURORA_STORAGE_CRC_OFFSET                   (12U)

/* 设置载荷内部字段偏移。 */
#define AURORA_STORAGE_CHEMISTRY_OFFSET             (0U)
#define AURORA_STORAGE_PACK_OFFSET                  (1U)
#define AURORA_STORAGE_LIFETIME_ENERGY_OFFSET       (4U)
#define AURORA_STORAGE_DAILY_ENERGY_OFFSET          (8U)
#define AURORA_STORAGE_REVISION_OFFSET              (12U)

/* Flash双页Journal运行状态。 */
typedef struct
{
    aurora_persistent_settings_t settings;           /* 当前已应用的持久化设置。 */
    uint32_t sequence;                               /* 最近有效页的单调递增序号。 */
    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */
    uint32_t dirty_since_ms;                         /* 首次变脏时间，用于合并多次写。 */
} aurora_storage_ctx_t;

void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx);
void aurora_storage_mark_dirty(aurora_storage_ctx_t *ctx, uint32_t now_ms);
bool aurora_storage_decode_page(const uint8_t *page,
                                size_t page_size,
                                aurora_persistent_settings_t *settings,
                                uint32_t *sequence);
size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
                                  uint8_t *page,
                                  size_t page_size,
                                  bool committed);
uint32_t aurora_storage_crc32(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
