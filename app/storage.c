#include "storage.h"

#include <string.h>

static void put_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

uint32_t aurora_storage_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t i;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }
    for (i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    }
    return ~crc;
}

void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    /* 与旧产品新机默认行为一致：铅酸、72V档。 */
    ctx->settings.chemistry = AURORA_CHEM_LEAD;
    ctx->settings.pack = AURORA_PACK_72V;
    ctx->settings.settings_revision = 1U;
    ctx->sequence = 1U;
}

void aurora_storage_mark_dirty(aurora_storage_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }
    if (!ctx->dirty)
    {
        ctx->dirty_since_ms = now_ms;
    }
    ctx->dirty = true;
}

size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
                                  uint8_t *page,
                                  size_t page_size,
                                  bool committed)
{
    const uint16_t payload_size = 16U;
    uint32_t crc;

    if ((ctx == NULL) || (page == NULL) || (page_size < AURORA_STORAGE_PAGE_SIZE))
    {
        return 0U;
    }

    memset(page, 0xFF, AURORA_STORAGE_PAGE_SIZE);
    put_u32_le(&page[0], AURORA_STORAGE_MAGIC);
    put_u16_le(&page[4], AURORA_STORAGE_VERSION);
    put_u16_le(&page[6], payload_size);
    put_u32_le(&page[8], ctx->sequence);

    page[AURORA_STORAGE_HEADER_SIZE + 0U] = (uint8_t)ctx->settings.chemistry;
    page[AURORA_STORAGE_HEADER_SIZE + 1U] = (uint8_t)ctx->settings.pack;
    put_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 4U], ctx->settings.lifetime_energy_wh);
    put_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 8U], ctx->settings.daily_energy_wh);
    put_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 12U], ctx->settings.settings_revision);

    crc = aurora_storage_crc32(&page[AURORA_STORAGE_HEADER_SIZE], payload_size);
    put_u32_le(&page[12], crc);
    put_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET], committed ? AURORA_STORAGE_COMMIT_MARKER : 0xFFFFFFFFUL);
    return AURORA_STORAGE_HEADER_SIZE + payload_size;
}

bool aurora_storage_decode_page(const uint8_t *page,
                                size_t page_size,
                                aurora_persistent_settings_t *settings,
                                uint32_t *sequence)
{
    uint16_t version;
    uint16_t payload_size;
    uint32_t expected_crc;
    uint32_t actual_crc;

    if ((page == NULL) || (settings == NULL) || (sequence == NULL) ||
        (page_size < AURORA_STORAGE_PAGE_SIZE))
    {
        return false;
    }
    if ((get_u32_le(&page[0]) != AURORA_STORAGE_MAGIC) ||
        (get_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET]) != AURORA_STORAGE_COMMIT_MARKER))
    {
        return false;
    }

    version = get_u16_le(&page[4]);
    payload_size = get_u16_le(&page[6]);
    if ((version != AURORA_STORAGE_VERSION) || (payload_size != 16U))
    {
        return false;
    }

    expected_crc = get_u32_le(&page[12]);
    actual_crc = aurora_storage_crc32(&page[AURORA_STORAGE_HEADER_SIZE], payload_size);
    if (expected_crc != actual_crc)
    {
        return false;
    }

    memset(settings, 0, sizeof(*settings));
    settings->chemistry = (aurora_battery_chem_t)page[AURORA_STORAGE_HEADER_SIZE + 0U];
    settings->pack = (aurora_battery_pack_t)page[AURORA_STORAGE_HEADER_SIZE + 1U];
    settings->lifetime_energy_wh = get_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 4U]);
    settings->daily_energy_wh = get_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 8U]);
    settings->settings_revision = get_u32_le(&page[AURORA_STORAGE_HEADER_SIZE + 12U]);
    *sequence = get_u32_le(&page[8]);

    return (settings->chemistry < AURORA_CHEM_COUNT) && (settings->pack < AURORA_PACK_COUNT);
}
