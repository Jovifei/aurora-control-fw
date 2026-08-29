#include "storage.h"

#include <string.h>

/* IEEE CRC-32初值。 */
#define STORAGE_CRC32_INITIAL                       (0xFFFFFFFFUL)
/* IEEE CRC-32反射多项式。 */
#define STORAGE_CRC32_POLYNOMIAL                    (0xEDB88320UL)
/* 擦除态Flash字值。 */
#define STORAGE_ERASED_WORD                         (0xFFFFFFFFUL)

/*---------------------------------------------------------------------------*
 * Name        : static void put_u16_le(uint8_t *destination, uint16_t value)
 * Input       : destination - 目标字节地址；value - 待写入数值
 * Output      : 无
 * Description : 把16位整数按小端字节序写入页缓冲区。
 *---------------------------------------------------------------------------*/
static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void put_u32_le(uint8_t *destination, uint32_t value)
 * Input       : destination - 目标字节地址；value - 待写入数值
 * Output      : 无
 * Description : 把32位整数按小端字节序写入页缓冲区。
 *---------------------------------------------------------------------------*/
static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t get_u16_le(const uint8_t *source)
 * Input       : source - 小端字节流地址
 * Output      : 还原后的16位整数
 * Description : 从页缓冲区读取一个小端16位整数。
 *---------------------------------------------------------------------------*/
static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8U));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t get_u32_le(const uint8_t *source)
 * Input       : source - 小端字节流地址
 * Output      : 还原后的32位整数
 * Description : 从页缓冲区读取一个小端32位整数。
 *---------------------------------------------------------------------------*/
static uint32_t get_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool energy_history_valid(
 *               const aurora_persistent_settings_t *settings)
 * Input       : settings - 待恢复的持久化设置
 * Output      : true表示历史数量、单调性和总累计边界均合法
 * Description : 24h历史保存的是生命周期累计Wh快照，必须单调不减且不能超过当前总量。
 *---------------------------------------------------------------------------*/
static bool energy_history_valid(const aurora_persistent_settings_t *settings)
{
    uint32_t previous;
    size_t index;

    if ((settings == NULL) || (settings->energy_history_count == 0U) ||
        (settings->energy_history_count > AURORA_ENERGY_HISTORY_POINT_COUNT))
    {
        return false;
    }

    previous = settings->energy_history_wh[0];
    if (previous > settings->lifetime_energy_wh)
    {
        return false;
    }
    for (index = 1U; index < settings->energy_history_count; ++index)
    {
        const uint32_t current = settings->energy_history_wh[index];
        if ((current < previous) || (current > settings->lifetime_energy_wh))
        {
            return false;
        }
        previous = current;
    }
    return true;
}


/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_storage_crc32(const uint8_t *data, size_t length)
 * Input       : data - 数据缓冲区；length - 数据长度
 * Output      : IEEE CRC-32；data为空时返回0
 * Description : 逐字节、逐位计算持久化载荷CRC，用于识别写入中断、掉电和随机位翻转。
 *---------------------------------------------------------------------------*/
uint32_t aurora_storage_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = STORAGE_CRC32_INITIAL;
    size_t index;
    uint8_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            /* 反射算法：最低位为1时异或多项式，否则只右移。 */
            crc = (crc >> 1U) ^
                  (((crc & 1U) != 0U) ? STORAGE_CRC32_POLYNOMIAL : 0U);
        }
    }
    return ~crc;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx)
 * Input       : ctx - 存储上下文
 * Output      : 无
 * Description : 初始化默认电池档案、24h滚动能量基准和双页Journal状态，不执行Flash写入。
 *---------------------------------------------------------------------------*/
void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->settings.chemistry = AURORA_CHEM_LEAD;
    ctx->settings.pack = AURORA_PACK_72V;
    ctx->settings.settings_revision = 1U;
    ctx->settings.energy_history_count = 1U;
    ctx->settings.energy_history_wh[0] = 0U;
    ctx->page_a_status = AURORA_STORAGE_PAGE_ERASED;
    ctx->page_b_status = AURORA_STORAGE_PAGE_ERASED;
    ctx->sequence = 1U;
}


/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_mark_dirty(aurora_storage_ctx_t *ctx, uint32_t now_ms)
 * Input       : ctx - 存储上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 标记RAM设置待保存；只记录第一次变脏时间，使连续设置命令可合并成一次Flash写。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx, uint8_t *page, size_t page_size, bool committed)
 * Input       : ctx - 存储上下文；page - 页缓冲区；page_size - 缓冲区大小；committed - 是否写入提交标记
 * Output      : 有效记录长度；参数无效或缓冲区过小时返回0
 * Description : 序列化页头与设置载荷并计算CRC；Commit Marker可留在擦除态，供Service最后单独编程。
 *---------------------------------------------------------------------------*/
size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
                                  uint8_t *page,
                                  size_t page_size,
                                  bool committed)
{
    uint8_t *payload;
    uint32_t crc;
    size_t index;

    if ((ctx == NULL) || (page == NULL) ||
        (page_size < AURORA_STORAGE_PAGE_SIZE) ||
        !energy_history_valid(&ctx->settings))
    {
        return 0U;
    }

    memset(page, 0xFF, AURORA_STORAGE_PAGE_SIZE);
    put_u32_le(&page[AURORA_STORAGE_MAGIC_OFFSET], AURORA_STORAGE_MAGIC);
    put_u16_le(&page[AURORA_STORAGE_VERSION_OFFSET], AURORA_STORAGE_VERSION);
    put_u16_le(&page[AURORA_STORAGE_LENGTH_OFFSET], AURORA_STORAGE_PAYLOAD_SIZE);
    put_u32_le(&page[AURORA_STORAGE_SEQUENCE_OFFSET], ctx->sequence);

    payload = &page[AURORA_STORAGE_HEADER_SIZE];
    payload[AURORA_STORAGE_CHEMISTRY_OFFSET] = (uint8_t)ctx->settings.chemistry;
    payload[AURORA_STORAGE_PACK_OFFSET] = (uint8_t)ctx->settings.pack;
    payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET] = ctx->settings.energy_history_count;
    payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET + 1U] = 0U;
    put_u32_le(&payload[AURORA_STORAGE_LIFETIME_ENERGY_OFFSET],
               ctx->settings.lifetime_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_DAILY_ENERGY_OFFSET],
               ctx->settings.daily_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_REVISION_OFFSET],
               ctx->settings.settings_revision);
    for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
    {
        put_u32_le(&payload[AURORA_STORAGE_ENERGY_HISTORY_OFFSET + (index * 4U)],
                   ctx->settings.energy_history_wh[index]);
    }

    crc = aurora_storage_crc32(payload, AURORA_STORAGE_PAYLOAD_SIZE);
    put_u32_le(&page[AURORA_STORAGE_CRC_OFFSET], crc);
    put_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET],
               committed ? AURORA_STORAGE_COMMIT_MARKER : STORAGE_ERASED_WORD);

    return AURORA_STORAGE_HEADER_SIZE + AURORA_STORAGE_PAYLOAD_SIZE;
}


/*---------------------------------------------------------------------------*
 * Name        : bool aurora_storage_decode_page(const uint8_t *page, size_t page_size, aurora_persistent_settings_t *settings, uint32_t *sequence)
 * Input       : page - 页缓冲区；page_size - 缓冲区大小；settings - 设置输出；sequence - 序号输出
 * Output      : true表示页头、Commit Marker、CRC和枚举范围全部有效
 * Description : 严格校验双页Journal记录；任何字段不一致都拒绝恢复，避免半写页进入运行配置。
 *---------------------------------------------------------------------------*/
aurora_storage_page_status_t aurora_storage_classify_page(
    const uint8_t *page,
    size_t page_size,
    aurora_persistent_settings_t *settings,
    uint32_t *sequence)
{
    const uint8_t *payload;
    uint16_t version;
    uint16_t payload_size;
    uint16_t expected_size;
    uint32_t expected_crc;
    uint32_t actual_crc;
    uint32_t magic;
    uint32_t commit;
    size_t index;

    if ((page == NULL) || (settings == NULL) || (sequence == NULL) ||
        (page_size < AURORA_STORAGE_PAGE_SIZE))
    {
        return AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }

    magic = get_u32_le(&page[AURORA_STORAGE_MAGIC_OFFSET]);
    commit = get_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET]);
    if ((magic == STORAGE_ERASED_WORD) && (commit == STORAGE_ERASED_WORD))
    {
        return AURORA_STORAGE_PAGE_ERASED;
    }
    if ((magic != AURORA_STORAGE_MAGIC) || (commit != AURORA_STORAGE_COMMIT_MARKER))
    {
        return AURORA_STORAGE_PAGE_INCOMPLETE;
    }

    version = get_u16_le(&page[AURORA_STORAGE_VERSION_OFFSET]);
    payload_size = get_u16_le(&page[AURORA_STORAGE_LENGTH_OFFSET]);
    if (version == AURORA_STORAGE_VERSION)
    {
        expected_size = AURORA_STORAGE_PAYLOAD_SIZE;
    }
    else if (version == AURORA_STORAGE_VERSION_LEGACY)
    {
        expected_size = AURORA_STORAGE_LEGACY_PAYLOAD_SIZE;
    }
    else
    {
        return AURORA_STORAGE_PAGE_VERSION_ERROR;
    }
    if (payload_size != expected_size)
    {
        return AURORA_STORAGE_PAGE_VERSION_ERROR;
    }

    payload = &page[AURORA_STORAGE_HEADER_SIZE];
    expected_crc = get_u32_le(&page[AURORA_STORAGE_CRC_OFFSET]);
    actual_crc = aurora_storage_crc32(payload, payload_size);
    if (expected_crc != actual_crc)
    {
        return AURORA_STORAGE_PAGE_CRC_ERROR;
    }

    memset(settings, 0, sizeof(*settings));
    settings->chemistry = (aurora_battery_chem_t)payload[AURORA_STORAGE_CHEMISTRY_OFFSET];
    settings->pack = (aurora_battery_pack_t)payload[AURORA_STORAGE_PACK_OFFSET];
    settings->lifetime_energy_wh =
        get_u32_le(&payload[AURORA_STORAGE_LIFETIME_ENERGY_OFFSET]);
    settings->settings_revision =
        get_u32_le(&payload[AURORA_STORAGE_REVISION_OFFSET]);
    *sequence = get_u32_le(&page[AURORA_STORAGE_SEQUENCE_OFFSET]);

    if ((settings->chemistry >= AURORA_CHEM_COUNT) ||
        (settings->pack >= AURORA_PACK_COUNT))
    {
        return AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }

    if (version == AURORA_STORAGE_VERSION_LEGACY)
    {
        /*
         * v1的daily字段是单调累计而非真正24h；迁移时不能伪装成有效窗口。
         * 从当前总累计建立第一个基准点，随后按30min逐步形成真实24h历史。
         */
        settings->daily_energy_wh = 0U;
        settings->energy_history_count = 1U;
        settings->energy_history_wh[0] = settings->lifetime_energy_wh;
        return AURORA_STORAGE_PAGE_VALID_LEGACY;
    }

    settings->energy_history_count = payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET];
    for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
    {
        settings->energy_history_wh[index] =
            get_u32_le(&payload[AURORA_STORAGE_ENERGY_HISTORY_OFFSET + (index * 4U)]);
    }
    if (!energy_history_valid(settings))
    {
        return AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }
    aurora_storage_energy_history_update(settings);
    return AURORA_STORAGE_PAGE_VALID;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_storage_decode_page(const uint8_t *page,
 *               size_t page_size, aurora_persistent_settings_t *settings,
 *               uint32_t *sequence)
 * Input       : page/page_size - Flash页；settings/sequence - 恢复输出
 * Output      : true表示当前或可迁移的旧版本记录有效
 * Description : 保留原布尔接口给旧调用方；详细失败原因由classify接口提供。
 *---------------------------------------------------------------------------*/
bool aurora_storage_decode_page(const uint8_t *page,
                                size_t page_size,
                                aurora_persistent_settings_t *settings,
                                uint32_t *sequence)
{
    const aurora_storage_page_status_t status =
        aurora_storage_classify_page(page, page_size, settings, sequence);
    return (status == AURORA_STORAGE_PAGE_VALID) ||
           (status == AURORA_STORAGE_PAGE_VALID_LEGACY);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_energy_history_update(
 *               aurora_persistent_settings_t *settings)
 * Input       : settings - 当前持久化设置
 * Output      : 无
 * Description : 用当前总累计减最老快照得到最近24h滚动量；历史不足24h时表示自首次有效快照以来能量。
 *---------------------------------------------------------------------------*/
void aurora_storage_energy_history_update(aurora_persistent_settings_t *settings)
{
    uint32_t oldest;

    if (settings == NULL)
    {
        return;
    }
    if ((settings->energy_history_count == 0U) ||
        (settings->energy_history_count > AURORA_ENERGY_HISTORY_POINT_COUNT))
    {
        memset(settings->energy_history_wh, 0, sizeof(settings->energy_history_wh));
        settings->energy_history_count = 1U;
        settings->energy_history_wh[0] = settings->lifetime_energy_wh;
    }

    oldest = settings->energy_history_wh[0];
    settings->daily_energy_wh =
        (settings->lifetime_energy_wh >= oldest) ?
            (settings->lifetime_energy_wh - oldest) : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_energy_history_checkpoint(
 *               aurora_persistent_settings_t *settings)
 * Input       : settings - 当前持久化设置
 * Output      : 无
 * Description : 每30min追加生命周期累计Wh快照；满49点后左移一格，使首尾始终覆盖最近48个间隔。
 *---------------------------------------------------------------------------*/
void aurora_storage_energy_history_checkpoint(aurora_persistent_settings_t *settings)
{
    if (settings == NULL)
    {
        return;
    }

    aurora_storage_energy_history_update(settings);
    if (settings->energy_history_count < AURORA_ENERGY_HISTORY_POINT_COUNT)
    {
        settings->energy_history_wh[settings->energy_history_count] =
            settings->lifetime_energy_wh;
        settings->energy_history_count++;
    }
    else
    {
        memmove(&settings->energy_history_wh[0],
                &settings->energy_history_wh[1],
                (AURORA_ENERGY_HISTORY_POINT_COUNT - 1U) *
                    sizeof(settings->energy_history_wh[0]));
        settings->energy_history_wh[AURORA_ENERGY_HISTORY_POINT_COUNT - 1U] =
            settings->lifetime_energy_wh;
    }
    aurora_storage_energy_history_update(settings);
}
