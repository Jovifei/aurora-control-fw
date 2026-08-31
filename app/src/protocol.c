#include "protocol.h"

#include "app_config.h"

#include <string.h>

/* 协议解析状态只在本文件内使用，数值存入aurora_protocol_ctx_t.step。 */
typedef enum
{
    PROTOCOL_STEP_SYNC_0 = 0,
    PROTOCOL_STEP_SYNC_1,
    PROTOCOL_STEP_LENGTH,
    PROTOCOL_STEP_ACTION,
    PROTOCOL_STEP_RESOURCE,
    PROTOCOL_STEP_MESSAGE_ID,
    PROTOCOL_STEP_DATA_LENGTH,
    PROTOCOL_STEP_DATA,
    PROTOCOL_STEP_CHECKSUM
} protocol_step_t;

/* 旧协议遥测载荷字段偏移；修改前必须同步外部协议文档和Golden Vector。 */
typedef enum
{
    TELEMETRY_OFFSET_PV_VOLTAGE = 0,
    TELEMETRY_OFFSET_PV_CURRENT = 2,
    TELEMETRY_OFFSET_DAILY_ENERGY_0 = 4,
    TELEMETRY_OFFSET_BAT_VOLTAGE = 6,
    TELEMETRY_OFFSET_BAT_CURRENT = 8,
    TELEMETRY_OFFSET_AMBIENT_TEMP = 10,
    TELEMETRY_OFFSET_CHEMISTRY = 11,
    TELEMETRY_OFFSET_PACK = 12,
    TELEMETRY_OFFSET_CHARGE_STAGE = 13,
    TELEMETRY_OFFSET_CHARGING = 14,
    TELEMETRY_OFFSET_LIFETIME_ENERGY = 15,
    TELEMETRY_OFFSET_MOS_TEMP = 19,
    TELEMETRY_OFFSET_DAILY_ENERGY_1 = 20,
    TELEMETRY_OFFSET_FAULT = 22,
    TELEMETRY_OFFSET_FW_MAJOR = 23,
    TELEMETRY_OFFSET_FW_MINOR = 24,
    TELEMETRY_OFFSET_FW_PATCH = 25,
    TELEMETRY_OFFSET_MODEL = 26
} telemetry_offset_t;

/*---------------------------------------------------------------------------*
 * Name        : static void put_u16_le(uint8_t *destination, uint16_t value)
 * Input       : destination - 目标字节地址；value - 16位输入值
 * Output      : 无
 * Description : 按旧协议载荷的小端格式写入16位整数。
 *---------------------------------------------------------------------------*/
static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void put_u32_le(uint8_t *destination, uint32_t value)
 * Input       : destination - 目标字节地址；value - 32位输入值
 * Output      : 无
 * Description : 按旧协议载荷的小端格式写入32位整数。
 *---------------------------------------------------------------------------*/
static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint8_t legacy_fault_code(uint32_t fault_mask)
 * Input       : fault_mask - 内部故障位图
 * Output      : 旧产品协议使用的单字节首要故障码
 * Description : 按产品兼容优先级把多位内部故障压缩为单字节；多个故障并存时只上报优先级最高者。
 *---------------------------------------------------------------------------*/
static uint8_t legacy_fault_code(uint32_t fault_mask)
{
    if ((fault_mask &
         (AURORA_FAULT_AMB_TEMP | AURORA_FAULT_AMB_NTC_OPEN | AURORA_FAULT_AMB_NTC_SHORT)) != 0U)
    {
        return 21U;
    }
    if ((fault_mask & (AURORA_FAULT_MOS_OVERTEMP | AURORA_FAULT_MOS_NTC_OPEN |
                       AURORA_FAULT_MOS_NTC_SHORT)) != 0U)
    {
        return 22U;
    }
    if ((fault_mask & (AURORA_FAULT_ADC_STALE | AURORA_FAULT_ADC_DMA | AURORA_FAULT_ADC_OVERRUN |
                       AURORA_FAULT_RELAY | AURORA_FAULT_STORAGE | AURORA_FAULT_INTERNAL |
                       AURORA_FAULT_FAST_BREAK | AURORA_FAULT_PV_CURRENT_PLAUSIBILITY |
                       AURORA_FAULT_BUS_ADC_SATURATION | AURORA_FAULT_BUS_OVERVOLT |
                       AURORA_FAULT_DEMO_OUTPUT)) != 0U)
    {
        return 23U;
    }
    if ((fault_mask & AURORA_FAULT_BAT_OVERVOLT) != 0U)
    {
        return 11U;
    }
    if ((fault_mask & AURORA_FAULT_FAST_MOS_OCP) != 0U)
    {
        return 12U;
    }
    if ((fault_mask & AURORA_FAULT_BAT_UNDERVOLT) != 0U)
    {
        return 13U;
    }
    if ((fault_mask & AURORA_FAULT_PV_OVERVOLT) != 0U)
    {
        return 1U;
    }
    if ((fault_mask & (AURORA_FAULT_FAST_PV_OCP | AURORA_FAULT_PV_OVERCURRENT |
                       AURORA_FAULT_PV_OVERPOWER)) != 0U)
    {
        return 2U;
    }
    if ((fault_mask & AURORA_FAULT_PV_UNDERVOLT) != 0U)
    {
        return 3U;
    }
    // 任意未知非零故障必须回退到硬件/内部故障码，绝不能上报“无故障”。
    return (fault_mask != 0U) ? 23U : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : static void parser_reset(aurora_protocol_ctx_t *ctx)
 * Input       : ctx - 协议解析上下文
 * Output      : 无
 * Description : 回到等待第一个同步字节的状态，保留累计错误计数和最后字节时间。
 *---------------------------------------------------------------------------*/
static void parser_reset(aurora_protocol_ctx_t *ctx)
{
    ctx->step = PROTOCOL_STEP_SYNC_0;
    ctx->item = 0U;
    ctx->length = 0U;
    ctx->checksum = 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protocol_init(aurora_protocol_ctx_t *ctx)
 * Input       : ctx - 协议解析上下文
 * Output      : 无
 * Description : 清零解析器、帧缓冲和错误统计，从等待同步字节状态开始。
 *---------------------------------------------------------------------------*/
void aurora_protocol_init(aurora_protocol_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(*ctx));
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protocol_feed_byte(aurora_protocol_ctx_t *ctx,
 *               uint8_t byte, uint32_t now_ms)
 * Input       : ctx - 协议解析上下文；byte - 本次接收字节；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 用有界状态机逐字节解析帧头、长度、字段、载荷和校验和；超时或错误时重同步。
 *---------------------------------------------------------------------------*/
void aurora_protocol_feed_byte(aurora_protocol_ctx_t *ctx, uint8_t byte, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }

    if ((ctx->step != PROTOCOL_STEP_SYNC_0) &&
        ((now_ms - ctx->last_byte_ms) > AURORA_PROTOCOL_RX_TIMEOUT_MS))
    {
        ctx->error_count++;
        parser_reset(ctx);
    }
    ctx->last_byte_ms = now_ms;

    /* step存储为字节；异常值仍按原default路径复位，不能进入状态机分支。 */
    if (ctx->step > (uint8_t)PROTOCOL_STEP_CHECKSUM)
    {
        parser_reset(ctx);
        return;
    }

    switch ((protocol_step_t)ctx->step)
    {
    case PROTOCOL_STEP_SYNC_0:
        if (byte == AURORA_PROTOCOL_SYNC_0)
        {
            memset(&ctx->frame, 0, sizeof(ctx->frame));
            ctx->checksum = byte;
            ctx->step = PROTOCOL_STEP_SYNC_1;
        }
        break;

    case PROTOCOL_STEP_SYNC_1:
        if (byte == AURORA_PROTOCOL_SYNC_1)
        {
            ctx->checksum = (uint8_t)(ctx->checksum + byte);
            ctx->step = PROTOCOL_STEP_LENGTH;
            ctx->item = 0U;
        }
        else if (byte == AURORA_PROTOCOL_SYNC_0)
        {
            /* 连续FA把当前字节直接视为新帧头，提高噪声后的重同步速度。 */
            ctx->checksum = byte;
            ctx->step = PROTOCOL_STEP_SYNC_1;
        }
        else
        {
            parser_reset(ctx);
        }
        break;

    case PROTOCOL_STEP_LENGTH:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        if (ctx->item == 0U)
        {
            ctx->length = (uint16_t)((uint16_t)byte << 8U);
            ctx->item = 1U;
        }
        else
        {
            ctx->length |= byte;
            ctx->item = 0U;
            if ((ctx->length < AURORA_PROTOCOL_BODY_OVERHEAD) ||
                (ctx->length > (AURORA_PROTOCOL_MAX_DATA + AURORA_PROTOCOL_BODY_OVERHEAD)))
            {
                ctx->error_count++;
                parser_reset(ctx);
            }
            else
            {
                ctx->step = PROTOCOL_STEP_ACTION;
            }
        }
        break;

    case PROTOCOL_STEP_ACTION:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.action = byte;
        ctx->step = PROTOCOL_STEP_RESOURCE;
        break;

    case PROTOCOL_STEP_RESOURCE:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        if (ctx->item == 0U)
        {
            ctx->frame.resource = (uint16_t)((uint16_t)byte << 8U);
            ctx->item = 1U;
        }
        else
        {
            ctx->frame.resource |= byte;
            ctx->item = 0U;
            ctx->step = PROTOCOL_STEP_MESSAGE_ID;
        }
        break;

    case PROTOCOL_STEP_MESSAGE_ID:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.message_id = (ctx->frame.message_id << 8U) | byte;
        ctx->item++;
        if (ctx->item >= sizeof(ctx->frame.message_id))
        {
            ctx->item = 0U;
            ctx->step = PROTOCOL_STEP_DATA_LENGTH;
        }
        break;

    case PROTOCOL_STEP_DATA_LENGTH:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        if (ctx->item == 0U)
        {
            ctx->frame.data_length = (uint16_t)((uint16_t)byte << 8U);
            ctx->item = 1U;
        }
        else
        {
            ctx->frame.data_length |= byte;
            ctx->item = 0U;
            if ((ctx->frame.data_length > AURORA_PROTOCOL_MAX_DATA) ||
                ((uint16_t)(ctx->frame.data_length + AURORA_PROTOCOL_BODY_OVERHEAD) != ctx->length))
            {
                ctx->error_count++;
                parser_reset(ctx);
            }
            else
            {
                ctx->step =
                    (ctx->frame.data_length == 0U) ? PROTOCOL_STEP_CHECKSUM : PROTOCOL_STEP_DATA;
            }
        }
        break;

    case PROTOCOL_STEP_DATA:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.data[ctx->item++] = byte;
        if (ctx->item >= ctx->frame.data_length)
        {
            ctx->item = 0U;
            ctx->step = PROTOCOL_STEP_CHECKSUM;
        }
        break;

    case PROTOCOL_STEP_CHECKSUM:
        if (ctx->checksum == byte)
        {
            ctx->frame_ready = true;
        }
        else
        {
            ctx->error_count++;
        }
        parser_reset(ctx);
        break;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protocol_take_frame(aurora_protocol_ctx_t *ctx,
 *               aurora_protocol_frame_t *out)
 * Input       : ctx - 协议解析上下文；out - 完整帧输出地址
 * Output      : true - 已取出一帧；false - 参数错误或当前无完整帧
 * Description : 复制并领取已校验帧，然后清除ready标志，避免后续接收覆盖未处理数据。
 *---------------------------------------------------------------------------*/
bool aurora_protocol_take_frame(aurora_protocol_ctx_t *ctx, aurora_protocol_frame_t *out)
{
    if ((ctx == NULL) || (out == NULL) || !ctx->frame_ready)
    {
        return false;
    }

    *out = ctx->frame;
    ctx->frame_ready = false;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : size_t aurora_protocol_encode(const aurora_protocol_frame_t *frame,
 *               uint8_t *wire, size_t capacity)
 * Input       : frame - 结构化协议帧；wire - 线格式输出缓冲；
 *               capacity - 输出缓冲容量
 * Output      : 实际编码字节数；0表示参数、长度或容量错误
 * Description : 按旧协议大端头字段、小端业务载荷约定编码线格式，并在末尾生成累加校验和。
 *---------------------------------------------------------------------------*/
size_t aurora_protocol_encode(const aurora_protocol_frame_t *frame, uint8_t *wire, size_t capacity)
{
    uint16_t body_length;
    size_t total_length;
    size_t index;
    uint8_t checksum = 0U;

    if ((frame == NULL) || (wire == NULL) || (frame->data_length > AURORA_PROTOCOL_MAX_DATA))
    {
        return 0U;
    }

    body_length = (uint16_t)(frame->data_length + AURORA_PROTOCOL_BODY_OVERHEAD);
    total_length = (size_t)frame->data_length + AURORA_PROTOCOL_WIRE_OVERHEAD;
    if (capacity < total_length)
    {
        return 0U;
    }

    wire[0] = AURORA_PROTOCOL_SYNC_0;
    wire[1] = AURORA_PROTOCOL_SYNC_1;
    wire[2] = (uint8_t)(body_length >> 8U);
    wire[3] = (uint8_t)body_length;
    wire[4] = frame->action;
    wire[5] = (uint8_t)(frame->resource >> 8U);
    wire[6] = (uint8_t)frame->resource;
    wire[7] = (uint8_t)(frame->message_id >> 24U);
    wire[8] = (uint8_t)(frame->message_id >> 16U);
    wire[9] = (uint8_t)(frame->message_id >> 8U);
    wire[10] = (uint8_t)frame->message_id;
    wire[11] = (uint8_t)(frame->data_length >> 8U);
    wire[12] = (uint8_t)frame->data_length;
    memcpy(&wire[13], frame->data, frame->data_length);

    for (index = 0U; index < (total_length - 1U); ++index)
    {
        checksum = (uint8_t)(checksum + wire[index]);
    }
    wire[total_length - 1U] = checksum;
    return total_length;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protocol_fill_telemetry_ex(aurora_protocol_frame_t *frame,
 *               uint32_t message_id, const aurora_measurement_t *sample,
 *               aurora_charge_state_t charge_state, uint32_t fault_mask,
 *               const aurora_persistent_settings_t *settings)
 * Input       : frame - 遥测帧输出；message_id - 消息ID；sample - 最新测量；
 *               charge_state - 充电阶段；fault_mask - 锁存故障；settings - 持久化设置
 * Output      : 无
 * Description : 按旧产品30字节字段位置填充遥测，保持现有APP/面板兼容性和字段字节序。
 *---------------------------------------------------------------------------*/
void aurora_protocol_fill_telemetry_ex(aurora_protocol_frame_t *frame, uint32_t message_id,
                                       const aurora_measurement_t *sample,
                                       aurora_charge_state_t charge_state,
                                       bool actual_power_transfer, uint32_t fault_mask,
                                       const aurora_persistent_settings_t *settings)
{
    uint8_t stage = 0U;

    if ((frame == NULL) || (sample == NULL) || (settings == NULL))
    {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->action = AURORA_PROTOCOL_ACTION_TELEMETRY;
    frame->resource = AURORA_PROTOCOL_RESOURCE_USER_DATA;
    frame->message_id = message_id;
    frame->data_length = AURORA_PROTOCOL_TELEMETRY_DATA_LENGTH;

    put_u16_le(&frame->data[TELEMETRY_OFFSET_PV_VOLTAGE],
               (uint16_t)((sample->pv_voltage_mv > 0) ? sample->pv_voltage_mv / 10 : 0));
    put_u16_le(&frame->data[TELEMETRY_OFFSET_PV_CURRENT],
               (uint16_t)((sample->pv_current_ma > 0) ? sample->pv_current_ma / 10 : 0));
    put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_0],
               (uint16_t)settings->charge_est_daily_energy_wh);
    put_u16_le(&frame->data[TELEMETRY_OFFSET_BAT_VOLTAGE],
               (uint16_t)((sample->battery_voltage_mv > 0) ? sample->battery_voltage_mv / 10 : 0));
    put_u16_le(
        &frame->data[TELEMETRY_OFFSET_BAT_CURRENT],
        (uint16_t)((sample->battery_current_est_ma > 0) ? sample->battery_current_est_ma / 10 : 0));
    frame->data[TELEMETRY_OFFSET_AMBIENT_TEMP] = (uint8_t)(sample->ambient_temp_dC / 10);
    frame->data[TELEMETRY_OFFSET_CHEMISTRY] = (uint8_t)settings->chemistry;
    frame->data[TELEMETRY_OFFSET_PACK] = (uint8_t)settings->pack;

    if ((charge_state >= AURORA_CHARGE_TRICKLE) && (charge_state <= AURORA_CHARGE_FLOAT))
    {
        stage = (uint8_t)(charge_state - AURORA_CHARGE_TRICKLE);
    }
    frame->data[TELEMETRY_OFFSET_CHARGE_STAGE] = stage;
    frame->data[TELEMETRY_OFFSET_CHARGING] = actual_power_transfer ? 1U : 0U;
    put_u32_le(&frame->data[TELEMETRY_OFFSET_LIFETIME_ENERGY],
               settings->charge_est_lifetime_energy_wh);
    frame->data[TELEMETRY_OFFSET_MOS_TEMP] = (uint8_t)(sample->mos_temp_dC / 10);
    put_u16_le(&frame->data[TELEMETRY_OFFSET_DAILY_ENERGY_1],
               (uint16_t)settings->charge_est_daily_energy_wh);
    frame->data[TELEMETRY_OFFSET_FAULT] = legacy_fault_code(fault_mask);
    frame->data[TELEMETRY_OFFSET_FW_MAJOR] = AURORA_FW_VERSION_MAJOR;
    frame->data[TELEMETRY_OFFSET_FW_MINOR] = AURORA_FW_VERSION_MINOR;
    frame->data[TELEMETRY_OFFSET_FW_PATCH] = AURORA_FW_VERSION_PATCH;
    memcpy(&frame->data[TELEMETRY_OFFSET_MODEL], AURORA_PRODUCT_MODEL,
           sizeof(AURORA_PRODUCT_MODEL) - 1U);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protocol_fill_telemetry(aurora_protocol_frame_t *frame,
 *               uint32_t message_id, const aurora_measurement_t *sample,
 *               aurora_charge_state_t charge_state, uint32_t fault_mask,
 *               const aurora_persistent_settings_t *settings)
 * Input       : 与历史遥测接口一致
 * Output      : 无
 * Description : 兼容旧调用方；按充电状态推导旧式charging标志，正式运行链使用扩展入口。
 *---------------------------------------------------------------------------*/
void aurora_protocol_fill_telemetry(aurora_protocol_frame_t *frame, uint32_t message_id,
                                    const aurora_measurement_t *sample,
                                    aurora_charge_state_t charge_state, uint32_t fault_mask,
                                    const aurora_persistent_settings_t *settings)
{
    const bool legacy_charging = (charge_state != AURORA_CHARGE_OFF) &&
                                 (charge_state != AURORA_CHARGE_COMPLETE) &&
                                 (charge_state != AURORA_CHARGE_FAULT);
    aurora_protocol_fill_telemetry_ex(frame, message_id, sample, charge_state, legacy_charging,
                                      fault_mask, settings);
}
