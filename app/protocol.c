#include "protocol.h"

#include "app_config.h"

#include <string.h>

#define RX_TIMEOUT_MS (50U)

static void parser_reset(aurora_protocol_ctx_t *ctx)
{
    ctx->step = 0U;
    ctx->item = 0U;
    ctx->length = 0U;
    ctx->checksum = 0U;
}

void aurora_protocol_init(aurora_protocol_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(*ctx));
    }
}

void aurora_protocol_feed_byte(aurora_protocol_ctx_t *ctx,
                               uint8_t byte,
                               uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }

    if ((ctx->step != 0U) && ((now_ms - ctx->last_byte_ms) > RX_TIMEOUT_MS))
    {
        ctx->error_count++;
        parser_reset(ctx);
    }
    ctx->last_byte_ms = now_ms;

    switch (ctx->step)
    {
    case 0U:
        if (byte == 0xFAU)
        {
            memset(&ctx->frame, 0, sizeof(ctx->frame));
            ctx->checksum = byte;
            ctx->step = 1U;
        }
        break;
    case 1U:
        if (byte == 0xCEU)
        {
            ctx->checksum = (uint8_t)(ctx->checksum + byte);
            ctx->step = 2U;
            ctx->item = 0U;
        }
        else if (byte == 0xFAU)
        {
            /* 连续出现FA时，把当前字节直接当作新帧头，提升噪声后的重同步能力。 */
            ctx->checksum = byte;
            ctx->step = 1U;
        }
        else
        {
            parser_reset(ctx);
        }
        break;
    case 2U:
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
            if ((ctx->length < 10U) || (ctx->length > (AURORA_PROTOCOL_MAX_DATA + 10U)))
            {
                ctx->error_count++;
                parser_reset(ctx);
            }
            else
            {
                ctx->step = 3U;
            }
        }
        break;
    case 3U:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.action = byte;
        ctx->step = 4U;
        break;
    case 4U:
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
            ctx->step = 5U;
        }
        break;
    case 5U:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.message_id = (ctx->frame.message_id << 8U) | byte;
        ctx->item++;
        if (ctx->item >= 4U)
        {
            ctx->item = 0U;
            ctx->step = 6U;
        }
        break;
    case 6U:
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
                ((uint16_t)(ctx->frame.data_length + 10U) != ctx->length))
            {
                ctx->error_count++;
                parser_reset(ctx);
            }
            else
            {
                ctx->step = (ctx->frame.data_length == 0U) ? 8U : 7U;
            }
        }
        break;
    case 7U:
        ctx->checksum = (uint8_t)(ctx->checksum + byte);
        ctx->frame.data[ctx->item++] = byte;
        if (ctx->item >= ctx->frame.data_length)
        {
            ctx->item = 0U;
            ctx->step = 8U;
        }
        break;
    case 8U:
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
    default:
        parser_reset(ctx);
        break;
    }
}

bool aurora_protocol_take_frame(aurora_protocol_ctx_t *ctx,
                                aurora_protocol_frame_t *out)
{
    if ((ctx == NULL) || (out == NULL) || !ctx->frame_ready)
    {
        return false;
    }
    *out = ctx->frame;
    ctx->frame_ready = false;
    return true;
}

size_t aurora_protocol_encode(const aurora_protocol_frame_t *frame,
                              uint8_t *wire,
                              size_t capacity)
{
    uint16_t length;
    size_t total;
    size_t i;
    uint8_t checksum = 0U;

    if ((frame == NULL) || (wire == NULL) || (frame->data_length > AURORA_PROTOCOL_MAX_DATA))
    {
        return 0U;
    }

    length = (uint16_t)(frame->data_length + 10U);
    total = (size_t)frame->data_length + 14U;
    if (capacity < total)
    {
        return 0U;
    }

    wire[0] = 0xFAU;
    wire[1] = 0xCEU;
    wire[2] = (uint8_t)(length >> 8U);
    wire[3] = (uint8_t)length;
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

    for (i = 0U; i < total - 1U; ++i)
    {
        checksum = (uint8_t)(checksum + wire[i]);
    }
    wire[total - 1U] = checksum;
    return total;
}

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


static uint8_t legacy_fault_code(uint32_t fault_mask)
{
    if ((fault_mask & AURORA_FAULT_AMB_TEMP) != 0U)
    {
        return 21U;
    }
    if ((fault_mask & AURORA_FAULT_MOS_OVERTEMP) != 0U)
    {
        return 22U;
    }
    if ((fault_mask & (AURORA_FAULT_ADC_STALE | AURORA_FAULT_ADC_DMA |
                       AURORA_FAULT_ADC_OVERRUN | AURORA_FAULT_RELAY |
                       AURORA_FAULT_STORAGE | AURORA_FAULT_INTERNAL |
                       AURORA_FAULT_FAST_BREAK)) != 0U)
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
    if ((fault_mask & AURORA_FAULT_FAST_PV_OCP) != 0U)
    {
        return 2U;
    }
    if ((fault_mask & AURORA_FAULT_PV_UNDERVOLT) != 0U)
    {
        return 3U;
    }
    return 0U;
}

void aurora_protocol_fill_telemetry(aurora_protocol_frame_t *frame,
                                    uint32_t message_id,
                                    const aurora_measurement_t *sample,
                                    aurora_charge_state_t charge_state,
                                    uint32_t fault_mask,
                                    const aurora_persistent_settings_t *settings)
{
    uint8_t stage = 0U;

    if ((frame == NULL) || (sample == NULL) || (settings == NULL))
    {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->action = 0x01U;
    frame->resource = AURORA_PROTOCOL_RESOURCE_USER_DATA;
    frame->message_id = message_id;
    frame->data_length = 30U;

    /* 30字节载荷保持旧产品字段位置和小端字节序。 */
    put_u16_le(&frame->data[0], (uint16_t)((sample->pv_voltage_mv > 0) ?
                                               sample->pv_voltage_mv / 10 : 0));
    put_u16_le(&frame->data[2], (uint16_t)((sample->pv_current_ma > 0) ?
                                               sample->pv_current_ma / 10 : 0));
    put_u16_le(&frame->data[4], (uint16_t)settings->daily_energy_wh);
    put_u16_le(&frame->data[6], (uint16_t)((sample->battery_voltage_mv > 0) ?
                                               sample->battery_voltage_mv / 10 : 0));
    put_u16_le(&frame->data[8], (uint16_t)((sample->battery_current_est_ma > 0) ?
                                               sample->battery_current_est_ma / 10 : 0));
    frame->data[10] = (uint8_t)(sample->ambient_temp_dC / 10);
    frame->data[11] = (uint8_t)settings->chemistry;
    frame->data[12] = (uint8_t)settings->pack;

    if ((charge_state >= AURORA_CHARGE_TRICKLE) && (charge_state <= AURORA_CHARGE_FLOAT))
    {
        stage = (uint8_t)(charge_state - AURORA_CHARGE_TRICKLE);
    }
    frame->data[13] = stage;
    frame->data[14] = ((charge_state != AURORA_CHARGE_OFF) &&
                       (charge_state != AURORA_CHARGE_COMPLETE) &&
                       (charge_state != AURORA_CHARGE_FAULT)) ? 1U : 0U;
    put_u32_le(&frame->data[15], settings->lifetime_energy_wh);
    frame->data[19] = (uint8_t)(sample->mos_temp_dC / 10);
    put_u16_le(&frame->data[20], (uint16_t)settings->daily_energy_wh);
    frame->data[22] = legacy_fault_code(fault_mask);
    frame->data[23] = AURORA_FW_VERSION_MAJOR;
    frame->data[24] = AURORA_FW_VERSION_MINOR;
    frame->data[25] = AURORA_FW_VERSION_PATCH;
    frame->data[26] = (uint8_t)AURORA_PRODUCT_MODEL[0];
    frame->data[27] = (uint8_t)AURORA_PRODUCT_MODEL[1];
    frame->data[28] = (uint8_t)AURORA_PRODUCT_MODEL[2];
    frame->data[29] = (uint8_t)AURORA_PRODUCT_MODEL[3];
}
