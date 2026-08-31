#include "storage.h"

#include "app_config.h"

#include <string.h>

#define STORAGE_CRC32_INITIAL (0xFFFFFFFFUL)
#define STORAGE_CRC32_POLYNOMIAL (0xEDB88320UL)
#define STORAGE_ERASED_WORD (0xFFFFFFFFUL)

/*---------------------------------------------------------------------------*
 * Name        : static void put_u16_le(uint8_t *destination, uint16_t value)
 * Input       : destination - 小端输出缓冲；value - 16位数值
 * Output      : 无
 * Description : 按小端顺序写入16位字段，避免直接类型转换造成未对齐访问。
 *---------------------------------------------------------------------------*/
static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void put_u32_le(uint8_t *destination, uint32_t value)
 * Input       : destination - 小端输出缓冲；value - 32位数值
 * Output      : 无
 * Description : 按小端顺序写入32位字段，保持Flash页格式与MCU对齐方式解耦。
 *---------------------------------------------------------------------------*/
static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void put_u64_le(uint8_t *destination, uint64_t value)
 * Input       : destination - 小端输出缓冲；value - 64位数值
 * Output      : 无
 * Description : 按小端顺序写入能量余数，避免Cortex-M0+执行非对齐64位访问。
 *---------------------------------------------------------------------------*/
static void put_u64_le(uint8_t *destination, uint64_t value)
{
    uint8_t index;
    for (index = 0U; index < 8U; ++index)
    {
        destination[index] = (uint8_t)(value >> (index * 8U));
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t get_u16_le(const uint8_t *source)
 * Input       : source - 小端输入字节流
 * Output      : 还原后的16位数值
 * Description : 从Flash页字节流安全读取16位字段。
 *---------------------------------------------------------------------------*/
static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8U));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t get_u32_le(const uint8_t *source)
 * Input       : source - 小端输入字节流
 * Output      : 还原后的32位数值
 * Description : 从Flash页字节流安全读取32位字段。
 *---------------------------------------------------------------------------*/
static uint32_t get_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) | ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint64_t get_u64_le(const uint8_t *source)
 * Input       : source - 小端输入字节流
 * Output      : 还原后的64位数值
 * Description : 从Flash页字节流逐字节恢复能量余数，避免未对齐访问。
 *---------------------------------------------------------------------------*/
static uint64_t get_u64_le(const uint8_t *source)
{
    uint64_t value = 0ULL;
    uint8_t index;
    for (index = 0U; index < 8U; ++index)
    {
        value |= (uint64_t)source[index] << (index * 8U);
    }
    return value;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool history_array_valid(const uint32_t *history,
 *               uint8_t count, uint32_t lifetime_wh)
 * Input       : history - 累计Wh快照；count - 有效点数；lifetime_wh - 当前总累计
 * Output      : true表示单调且不超过总累计
 * Description : 两套24h历史均保存累计值，必须单调不减。
 *---------------------------------------------------------------------------*/
static bool history_array_valid(const uint32_t *history, uint8_t count, uint32_t lifetime_wh)
{
    uint32_t previous;
    size_t index;

    if ((history == NULL) || (count == 0U) || (count > AURORA_ENERGY_HISTORY_POINT_COUNT))
    {
        return false;
    }
    previous = history[0];
    if (previous > lifetime_wh)
    {
        return false;
    }
    for (index = 1U; index < count; ++index)
    {
        const uint32_t current = history[index];
        if ((current < previous) || (current > lifetime_wh))
        {
            return false;
        }
        previous = current;
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool settings_valid(const aurora_persistent_settings_t *settings)
 * Input       : settings - 待验证的v3持久化设置
 * Output      : true表示模式、参数和两套能量历史均满足内容约束
 * Description : 在编码或接纳Flash页前做内容级校验，防止CRC正确但字段越界的数据被应用。
 *---------------------------------------------------------------------------*/
static bool settings_valid(const aurora_persistent_settings_t *settings)
{
    return (settings != NULL) && (settings->chemistry < AURORA_CHEM_COUNT) &&
           (settings->pack < AURORA_PACK_COUNT) && (settings->operating_mode < AURORA_MODE_COUNT) &&
           (settings->demo_target_voltage_mv > 0U) &&
           (settings->demo_target_voltage_mv <= AURORA_DEMO_MAX_TARGET_VOLTAGE_MV) &&
           (settings->demo_power_limit_mw > 0U) &&
           (settings->demo_power_limit_mw <= AURORA_RATED_POWER_MW) &&
           (settings->history_interval_elapsed_ms < AURORA_ENERGY_HISTORY_INTERVAL_MS) &&
           history_array_valid(settings->energy_history_wh, settings->energy_history_count,
                               settings->lifetime_energy_wh) &&
           history_array_valid(settings->charge_est_history_wh, settings->energy_history_count,
                               settings->charge_est_lifetime_energy_wh);
}

/*---------------------------------------------------------------------------*
 * Name        : static void migrate_v1_v2(const uint8_t *payload,
 *               uint16_t version, aurora_persistent_settings_t *settings)
 * Input       : payload - 旧页载荷；version - 1或2；settings - v3输出
 * Output      : 无
 * Description : 旧单能量字段来自PV输入功率，迁移为PV实测账本；电池侧估算账本从0重新建立。
 *---------------------------------------------------------------------------*/
static void migrate_v1_v2(const uint8_t *payload, uint16_t version,
                          aurora_persistent_settings_t *settings)
{
    size_t index;

    settings->chemistry = (aurora_battery_chem_t)payload[AURORA_STORAGE_CHEMISTRY_OFFSET];
    settings->pack = (aurora_battery_pack_t)payload[AURORA_STORAGE_PACK_OFFSET];
    settings->operating_mode = AURORA_MODE_BATTERY;
    settings->demo_target_voltage_mv = AURORA_DEMO_TARGET_VOLTAGE_MV;
    settings->demo_power_limit_mw = AURORA_DEMO_POWER_LIMIT_MW;
    settings->energy_semantics_version = AURORA_ENERGY_SEMANTICS_VERSION;
    settings->lifetime_energy_wh = get_u32_le(&payload[AURORA_STORAGE_V2_LIFETIME_OFFSET]);
    settings->settings_revision = get_u32_le(&payload[AURORA_STORAGE_V2_REVISION_OFFSET]);

    if (version == AURORA_STORAGE_VERSION_V2)
    {
        settings->energy_history_count = payload[AURORA_STORAGE_V2_HISTORY_COUNT_OFFSET];
        for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
        {
            settings->energy_history_wh[index] =
                get_u32_le(&payload[AURORA_STORAGE_V2_HISTORY_OFFSET + index * 4U]);
        }
    }
    else
    {
        settings->energy_history_count = 1U;
        settings->energy_history_wh[0] = settings->lifetime_energy_wh;
    }

    // 新增的电池侧估算账本没有旧数据证据，从零基准开始，禁止伪装成实测历史。
    settings->charge_est_lifetime_energy_wh = 0U;
    settings->charge_est_history_wh[0] = 0U;
    if ((settings->energy_history_count == 0U) ||
        (settings->energy_history_count > AURORA_ENERGY_HISTORY_POINT_COUNT))
    {
        settings->energy_history_count = 1U;
        settings->energy_history_wh[0] = settings->lifetime_energy_wh;
    }
    for (index = 1U; index < settings->energy_history_count; ++index)
    {
        settings->charge_est_history_wh[index] = 0U;
    }
    aurora_storage_energy_history_update(settings);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_storage_crc32(const uint8_t *data, size_t length)
 * Input       : data - 待校验字节流；length - 字节数
 * Output      : IEEE CRC32结果；data为空时返回0
 * Description : 计算Flash Journal载荷CRC，用于识别掉电半写和内容损坏。
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
            crc = (crc >> 1U) ^ (((crc & 1U) != 0U) ? STORAGE_CRC32_POLYNOMIAL : 0U);
        }
    }
    return ~crc;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_init_defaults(aurora_storage_ctx_t *ctx)
 * Input       : ctx - 存储上下文
 * Output      : 无
 * Description : 初始化安全默认设置、双能量账本和双页状态；不在此函数内访问Flash。
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
    ctx->settings.operating_mode = AURORA_MODE_BATTERY;
    ctx->settings.demo_target_voltage_mv = AURORA_DEMO_TARGET_VOLTAGE_MV;
    ctx->settings.demo_power_limit_mw = AURORA_DEMO_POWER_LIMIT_MW;
    ctx->settings.settings_revision = 1U;
    ctx->settings.energy_semantics_version = AURORA_ENERGY_SEMANTICS_VERSION;
    ctx->settings.energy_history_count = 1U;
    ctx->page_a_status = AURORA_STORAGE_PAGE_ERASED;
    ctx->page_b_status = AURORA_STORAGE_PAGE_ERASED;
    ctx->sequence = 1U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_mark_dirty(aurora_storage_ctx_t *ctx, uint32_t now_ms)
 * Input       : ctx - 存储上下文；now_ms - 首次变脏时间
 * Output      : 无
 * Description : 标记RAM设置待保存；真正擦写由运行层在PWM关闭且Relay断开时执行。
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
 * Name        : size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
 *               uint8_t *page, size_t page_size, bool committed)
 * Input       : ctx - 当前设置；page/page_size - 目标页；committed - 是否写Commit Marker
 * Output      : 编码字节数；参数或内容无效时返回0
 * Description : 将v3设置编码为固定小端Flash页；Commit Marker由调用者在事务最后写入。
 *---------------------------------------------------------------------------*/
size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx, uint8_t *page, size_t page_size,
                                  bool committed)
{
    uint8_t *payload;
    size_t index;

    if ((ctx == NULL) || (page == NULL) || (page_size < AURORA_STORAGE_PAGE_SIZE) ||
        !settings_valid(&ctx->settings))
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
    payload[AURORA_STORAGE_MODE_OFFSET] = (uint8_t)ctx->settings.operating_mode;
    payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET] = ctx->settings.energy_history_count;
    payload[AURORA_STORAGE_ENERGY_SEMANTICS_OFFSET] = ctx->settings.energy_semantics_version;
    put_u32_le(&payload[AURORA_STORAGE_LIFETIME_ENERGY_OFFSET], ctx->settings.lifetime_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_DAILY_ENERGY_OFFSET], ctx->settings.daily_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_CHARGE_LIFETIME_OFFSET],
               ctx->settings.charge_est_lifetime_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_CHARGE_DAILY_OFFSET],
               ctx->settings.charge_est_daily_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_REVISION_OFFSET], ctx->settings.settings_revision);
    put_u32_le(&payload[AURORA_STORAGE_HISTORY_ELAPSED_OFFSET],
               ctx->settings.history_interval_elapsed_ms);
    put_u32_le(&payload[AURORA_STORAGE_DEMO_VOLTAGE_OFFSET], ctx->settings.demo_target_voltage_mv);
    put_u32_le(&payload[AURORA_STORAGE_DEMO_POWER_OFFSET], ctx->settings.demo_power_limit_mw);
    put_u64_le(&payload[AURORA_STORAGE_PV_REMAINDER_OFFSET],
               ctx->settings.pv_energy_remainder_mw_ms);
    put_u64_le(&payload[AURORA_STORAGE_CHARGE_REMAINDER_OFFSET],
               ctx->settings.charge_est_energy_remainder_mw_ms);

    for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
    {
        put_u32_le(&payload[AURORA_STORAGE_ENERGY_HISTORY_OFFSET + index * 4U],
                   ctx->settings.energy_history_wh[index]);
        put_u32_le(&payload[AURORA_STORAGE_CHARGE_HISTORY_OFFSET + index * 4U],
                   ctx->settings.charge_est_history_wh[index]);
    }

    put_u32_le(&page[AURORA_STORAGE_CRC_OFFSET],
               aurora_storage_crc32(payload, AURORA_STORAGE_PAYLOAD_SIZE));
    put_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET],
               committed ? AURORA_STORAGE_COMMIT_MARKER : STORAGE_ERASED_WORD);
    return AURORA_STORAGE_HEADER_SIZE + AURORA_STORAGE_PAYLOAD_SIZE;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_storage_page_status_t aurora_storage_classify_page(
 *               const uint8_t *page, size_t page_size, uint32_t *sequence_out)
 * Input       : page/page_size - 原始Flash页；sequence_out - 可选序号输出
 * Output      : 有效、旧版、擦除、半写、版本、CRC或内容错误分类
 * Description : 先判事务完整性，再校验版本、CRC与字段内容，供双页仲裁和自愈使用。
 *---------------------------------------------------------------------------*/
aurora_storage_page_status_t aurora_storage_classify_page(const uint8_t *page, size_t page_size,
                                                          aurora_persistent_settings_t *settings,
                                                          uint32_t *sequence)
{
    const uint8_t *payload;
    uint16_t version;
    uint16_t payload_size;
    uint16_t expected_size;
    uint32_t expected_crc;
    size_t index;

    if ((page == NULL) || (settings == NULL) || (sequence == NULL) ||
        (page_size < AURORA_STORAGE_PAGE_SIZE))
    {
        return AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }
    if ((get_u32_le(&page[AURORA_STORAGE_MAGIC_OFFSET]) == STORAGE_ERASED_WORD) &&
        (get_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET]) == STORAGE_ERASED_WORD))
    {
        return AURORA_STORAGE_PAGE_ERASED;
    }
    if ((get_u32_le(&page[AURORA_STORAGE_MAGIC_OFFSET]) != AURORA_STORAGE_MAGIC) ||
        (get_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET]) != AURORA_STORAGE_COMMIT_MARKER))
    {
        return AURORA_STORAGE_PAGE_INCOMPLETE;
    }

    version = get_u16_le(&page[AURORA_STORAGE_VERSION_OFFSET]);
    payload_size = get_u16_le(&page[AURORA_STORAGE_LENGTH_OFFSET]);
    if (version == AURORA_STORAGE_VERSION)
    {
        expected_size = AURORA_STORAGE_PAYLOAD_SIZE;
    }
    else if (version == AURORA_STORAGE_VERSION_V2)
    {
        expected_size = AURORA_STORAGE_V2_PAYLOAD_SIZE;
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
    if (expected_crc != aurora_storage_crc32(payload, payload_size))
    {
        return AURORA_STORAGE_PAGE_CRC_ERROR;
    }

    memset(settings, 0, sizeof(*settings));
    *sequence = get_u32_le(&page[AURORA_STORAGE_SEQUENCE_OFFSET]);
    if (version != AURORA_STORAGE_VERSION)
    {
        migrate_v1_v2(payload, version, settings);
        return ((settings->chemistry < AURORA_CHEM_COUNT) && (settings->pack < AURORA_PACK_COUNT) &&
                history_array_valid(settings->energy_history_wh, settings->energy_history_count,
                                    settings->lifetime_energy_wh))
                   ? AURORA_STORAGE_PAGE_VALID_LEGACY
                   : AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }

    settings->chemistry = (aurora_battery_chem_t)payload[AURORA_STORAGE_CHEMISTRY_OFFSET];
    settings->pack = (aurora_battery_pack_t)payload[AURORA_STORAGE_PACK_OFFSET];
    settings->operating_mode = (aurora_operating_mode_t)payload[AURORA_STORAGE_MODE_OFFSET];
    settings->energy_history_count = payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET];
    settings->energy_semantics_version = payload[AURORA_STORAGE_ENERGY_SEMANTICS_OFFSET];
    settings->lifetime_energy_wh = get_u32_le(&payload[AURORA_STORAGE_LIFETIME_ENERGY_OFFSET]);
    settings->daily_energy_wh = get_u32_le(&payload[AURORA_STORAGE_DAILY_ENERGY_OFFSET]);
    settings->charge_est_lifetime_energy_wh =
        get_u32_le(&payload[AURORA_STORAGE_CHARGE_LIFETIME_OFFSET]);
    settings->charge_est_daily_energy_wh = get_u32_le(&payload[AURORA_STORAGE_CHARGE_DAILY_OFFSET]);
    settings->settings_revision = get_u32_le(&payload[AURORA_STORAGE_REVISION_OFFSET]);
    settings->history_interval_elapsed_ms =
        get_u32_le(&payload[AURORA_STORAGE_HISTORY_ELAPSED_OFFSET]);
    settings->demo_target_voltage_mv = get_u32_le(&payload[AURORA_STORAGE_DEMO_VOLTAGE_OFFSET]);
    settings->demo_power_limit_mw = get_u32_le(&payload[AURORA_STORAGE_DEMO_POWER_OFFSET]);
    settings->pv_energy_remainder_mw_ms = get_u64_le(&payload[AURORA_STORAGE_PV_REMAINDER_OFFSET]);
    settings->charge_est_energy_remainder_mw_ms =
        get_u64_le(&payload[AURORA_STORAGE_CHARGE_REMAINDER_OFFSET]);
    for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
    {
        settings->energy_history_wh[index] =
            get_u32_le(&payload[AURORA_STORAGE_ENERGY_HISTORY_OFFSET + index * 4U]);
        settings->charge_est_history_wh[index] =
            get_u32_le(&payload[AURORA_STORAGE_CHARGE_HISTORY_OFFSET + index * 4U]);
    }
    if (!settings_valid(settings) ||
        (settings->energy_semantics_version != AURORA_ENERGY_SEMANTICS_VERSION))
    {
        return AURORA_STORAGE_PAGE_CONTENT_ERROR;
    }
    aurora_storage_energy_history_update(settings);
    return AURORA_STORAGE_PAGE_VALID;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_storage_decode_page(const uint8_t *page,
 *               size_t page_size, aurora_storage_ctx_t *ctx)
 * Input       : page/page_size - 原始页；ctx - 解码输出
 * Output      : true表示页面可用且已转换为当前v3内存格式
 * Description : 解码当前或旧版页；v1/v2只迁移有证据的PV能量，禁止伪造电池侧实测历史。
 *---------------------------------------------------------------------------*/
bool aurora_storage_decode_page(const uint8_t *page, size_t page_size,
                                aurora_persistent_settings_t *settings, uint32_t *sequence)
{
    const aurora_storage_page_status_t status =
        aurora_storage_classify_page(page, page_size, settings, sequence);
    return (status == AURORA_STORAGE_PAGE_VALID) || (status == AURORA_STORAGE_PAGE_VALID_LEGACY);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_energy_history_update(aurora_persistent_settings_t *settings)
 * Input       : settings - 双能量历史与累计值
 * Output      : 无
 * Description : 根据49点累计快照更新最近24h的PV实测量和电池侧估算量。
 *---------------------------------------------------------------------------*/
void aurora_storage_energy_history_update(aurora_persistent_settings_t *settings)
{
    uint32_t pv_oldest;
    uint32_t charge_oldest;

    if (settings == NULL)
    {
        return;
    }
    if ((settings->energy_history_count == 0U) ||
        (settings->energy_history_count > AURORA_ENERGY_HISTORY_POINT_COUNT))
    {
        memset(settings->energy_history_wh, 0, sizeof(settings->energy_history_wh));
        memset(settings->charge_est_history_wh, 0, sizeof(settings->charge_est_history_wh));
        settings->energy_history_count = 1U;
        settings->energy_history_wh[0] = settings->lifetime_energy_wh;
        settings->charge_est_history_wh[0] = settings->charge_est_lifetime_energy_wh;
    }
    pv_oldest = settings->energy_history_wh[0];
    charge_oldest = settings->charge_est_history_wh[0];
    settings->daily_energy_wh =
        (settings->lifetime_energy_wh >= pv_oldest) ? settings->lifetime_energy_wh - pv_oldest : 0U;
    settings->charge_est_daily_energy_wh =
        (settings->charge_est_lifetime_energy_wh >= charge_oldest)
            ? settings->charge_est_lifetime_energy_wh - charge_oldest
            : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_storage_energy_history_checkpoint(
 *               aurora_persistent_settings_t *settings)
 * Input       : settings - 当前累计量及历史窗口
 * Output      : 无
 * Description : 每30min插入一对累计快照；窗口满后左移，保持49个端点覆盖最近24h。
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
        const size_t index = settings->energy_history_count;
        settings->energy_history_wh[index] = settings->lifetime_energy_wh;
        settings->charge_est_history_wh[index] = settings->charge_est_lifetime_energy_wh;
        settings->energy_history_count++;
    }
    else
    {
        memmove(&settings->energy_history_wh[0], &settings->energy_history_wh[1],
                (AURORA_ENERGY_HISTORY_POINT_COUNT - 1U) * sizeof(uint32_t));
        memmove(&settings->charge_est_history_wh[0], &settings->charge_est_history_wh[1],
                (AURORA_ENERGY_HISTORY_POINT_COUNT - 1U) * sizeof(uint32_t));
        settings->energy_history_wh[AURORA_ENERGY_HISTORY_POINT_COUNT - 1U] =
            settings->lifetime_energy_wh;
        settings->charge_est_history_wh[AURORA_ENERGY_HISTORY_POINT_COUNT - 1U] =
            settings->charge_est_lifetime_energy_wh;
    }
    settings->history_interval_elapsed_ms = 0U;
    aurora_storage_energy_history_update(settings);
}
