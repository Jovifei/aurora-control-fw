#ifndef AURORA_STORAGE_H
#define AURORA_STORAGE_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AURORA_STORAGE_PAGE_SIZE       (512U)
#define AURORA_STORAGE_HEADER_SIZE     (24U)
#define AURORA_STORAGE_COMMIT_OFFSET   (16U)
#define AURORA_STORAGE_COMMIT_MARKER   (0xA55AA55AUL)
#define AURORA_STORAGE_MAGIC           (0x41555241UL)
#define AURORA_STORAGE_VERSION         (1U)

typedef struct
{
    aurora_persistent_settings_t settings;
    uint32_t sequence;
    bool dirty;
    uint32_t dirty_since_ms;
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
