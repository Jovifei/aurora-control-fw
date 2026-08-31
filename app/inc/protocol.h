#ifndef AURORA_PROTOCOL_H
#define AURORA_PROTOCOL_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 旧产品协议的第一个同步字节。 */
#define AURORA_PROTOCOL_SYNC_0                      (0xFAU)
/* 旧产品协议的第二个同步字节。 */
#define AURORA_PROTOCOL_SYNC_1                      (0xCEU)
/* length字段中除data外固定包含的字节数。 */
#define AURORA_PROTOCOL_BODY_OVERHEAD               (10U)
/* 线格式总长度相对data_length增加的字节数。 */
#define AURORA_PROTOCOL_WIRE_OVERHEAD               (14U)
/* 最大合法线格式帧长度。 */
#define AURORA_PROTOCOL_MAX_WIRE                    (AURORA_PROTOCOL_MAX_DATA + \
                                                     AURORA_PROTOCOL_WIRE_OVERHEAD)
/* 帧内相邻字节允许的最大间隔，单位ms。 */
#define AURORA_PROTOCOL_RX_TIMEOUT_MS               (50U)

/* 主动遥测/读取类动作码。 */
#define AURORA_PROTOCOL_ACTION_TELEMETRY            (0x01U)
/* 设置或复位请求动作码。 */
#define AURORA_PROTOCOL_ACTION_WRITE                (0x02U)
/* 请求应答动作码。 */
#define AURORA_PROTOCOL_ACTION_RESPONSE             (0x82U)

/* 设置资源：电池化学体系与电压档位。 */
#define AURORA_PROTOCOL_RESOURCE_USER_DATA          (10001U)
/* 设置资源：写入电池档案。 */
#define AURORA_PROTOCOL_RESOURCE_SETTING            (10002U)
/* 复位资源：清除累计能量。 */
#define AURORA_PROTOCOL_RESOURCE_RESET              (10003U)
/* 运行模式资源：0=Battery，1=Demo Load。 */
#define AURORA_PROTOCOL_RESOURCE_OPERATING_MODE     (10004U)
/* Demo参数资源：目标电压mV + 功率上限mW，均小端uint32。 */
#define AURORA_PROTOCOL_RESOURCE_DEMO_CONFIG        (10005U)

/* 命令执行成功返回值。 */
#define AURORA_PROTOCOL_RESULT_OK                   (0U)
/* 命令参数或长度无效返回值。 */
#define AURORA_PROTOCOL_RESULT_INVALID              (1U)
/* 设置命令固定载荷长度：chemistry + pack。 */
#define AURORA_PROTOCOL_SETTING_DATA_LENGTH         (2U)
/* 命令应答固定载荷长度：result。 */
#define AURORA_PROTOCOL_RESULT_DATA_LENGTH          (1U)
#define AURORA_PROTOCOL_MODE_DATA_LENGTH            (1U)
#define AURORA_PROTOCOL_DEMO_CONFIG_DATA_LENGTH     (8U)
/* 旧产品遥测载荷固定长度。 */
#define AURORA_PROTOCOL_TELEMETRY_DATA_LENGTH       (30U)

typedef struct
{
    uint32_t message_id;                          /* 请求/应答关联ID。 */
    uint16_t resource;                            /* 资源号。 */
    uint16_t data_length;                         /* data有效字节数。 */
    uint8_t action;                               /* 动作码。 */
    uint8_t data[AURORA_PROTOCOL_MAX_DATA];       /* 协议载荷。 */
} aurora_protocol_frame_t;

typedef struct
{
    aurora_protocol_frame_t frame;                /* 正在接收的帧。 */
    uint32_t last_byte_ms;                        /* 上一字节时间戳。 */
    uint32_t error_count;                         /* 超时/长度/校验错误累计。 */
    uint16_t item;                                /* 当前字段内字节索引。 */
    uint16_t length;                              /* 线格式length字段。 */
    uint8_t step;                                 /* 当前解析状态。 */
    uint8_t checksum;                             /* 累加校验和。 */
    bool frame_ready;                             /* 完整帧等待领取。 */
    uint8_t frame_ready_reserved;                 /* 显式补齐解析状态，避免隐式填充。 */
} aurora_protocol_ctx_t;

void aurora_protocol_init(aurora_protocol_ctx_t *ctx);
void aurora_protocol_feed_byte(aurora_protocol_ctx_t *ctx,
                               uint8_t byte,
                               uint32_t now_ms);
bool aurora_protocol_take_frame(aurora_protocol_ctx_t *ctx,
                                aurora_protocol_frame_t *out);
size_t aurora_protocol_encode(const aurora_protocol_frame_t *frame,
                              uint8_t *wire,
                              size_t capacity);
void aurora_protocol_fill_telemetry(aurora_protocol_frame_t *frame,
                                    uint32_t message_id,
                                    const aurora_measurement_t *sample,
                                    aurora_charge_state_t charge_state,
                                    uint32_t fault_mask,
                                    const aurora_persistent_settings_t *settings); // 兼容旧遥测调用入口
void aurora_protocol_fill_telemetry_ex(aurora_protocol_frame_t *frame,
                                       uint32_t message_id,
                                       const aurora_measurement_t *sample,
                                       aurora_charge_state_t charge_state,
                                       bool actual_power_transfer,
                                       uint32_t fault_mask,
                                       const aurora_persistent_settings_t *settings); // 使用真实功率传输状态填充遥测

#ifdef __cplusplus
}
#endif

#endif
