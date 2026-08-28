#include "driver.h"
#include "mock_driver.h"

#include <string.h>

/* Host模拟ADC采用双半缓冲，与目标DMA发布模型保持一致。 */
#define MOCK_ADC_BLOCK_COUNT                 (2U)
/* Host模拟UART发送缓存容量，仅用于协议回归测试。 */
#define MOCK_UART_TX_CAPACITY                (512U)
/* G32F031片内Flash最后1 KiB保留区的起始地址。 */
#define MOCK_FLASH_BASE_ADDRESS              (0x0000FC00UL)
/* G32F031片内Flash地址上界，采用左闭右开区间。 */
#define MOCK_FLASH_END_ADDRESS               (0x00010000UL)
/* Host模拟Flash保留区的总字节数。 */
#define MOCK_FLASH_SIZE_BYTES                (1024U)
/* 目标芯片物理Flash页大小。 */
#define MOCK_FLASH_PAGE_SIZE_BYTES           (512U)

static uint32_t g_ms;
static uint16_t g_adc[MOCK_ADC_BLOCK_COUNT][DRV_ADC_BLOCK_WORDS];
static uint32_t g_staged_sequence;
static uint32_t g_applied_sequence;
static uint16_t g_duty;
static bool g_pwm_active;
static bool g_break_source;
static bool g_break_latched;
static bool g_relay;
static bool g_link;
static bool g_run_led;
static bool g_fault_led;
static uint32_t g_watchdog_feeds;
static uint8_t g_uart_tx[MOCK_UART_TX_CAPACITY];
static size_t g_uart_tx_length;
static uint8_t g_flash[MOCK_FLASH_SIZE_BYTES];

/*---------------------------------------------------------------------------*
 * Name        : static bool flash_range(uint32_t address, size_t length, size_t *offset)
 * Input       : address - 待读写的目标Flash地址；length - 访问长度；offset - Host数组偏移输出指针
 * Output      : true表示地址区间完整落在参数保留区；false表示越界、溢出或输出指针无效
 * Description : 把目标Flash地址映射为Host数组下标，并使用64位加法避免address+length溢出绕过边界检查。
 *---------------------------------------------------------------------------*/
static bool flash_range(uint32_t address, size_t length, size_t *offset)
{
    const uint64_t range_end = (uint64_t)address + (uint64_t)length;

    if (offset == NULL) {
        return false;
    }

    if ((address < MOCK_FLASH_BASE_ADDRESS) ||
        (range_end > (uint64_t)MOCK_FLASH_END_ADDRESS)) {
        return false;
    }

    *offset = (size_t)(address - MOCK_FLASH_BASE_ADDRESS);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void mock_reset(void)
 * Input       : 无
 * Output      : 无
 * Description : 清空所有Host模拟外设状态，并把模拟Flash恢复为擦除态0xFF，确保每个测试用例从确定基线开始。
 *---------------------------------------------------------------------------*/
void mock_reset(void)
{
    g_ms = 0U;
    memset(g_adc, 0, sizeof(g_adc));
    g_staged_sequence = 0U;
    g_applied_sequence = 0U;
    g_duty = 0U;
    g_pwm_active = false;
    g_break_source = false;
    g_break_latched = false;
    g_relay = false;
    g_link = false;
    g_run_led = false;
    g_fault_led = false;
    g_watchdog_feeds = 0U;
    g_uart_tx_length = 0U;
    memset(g_flash, 0xFF, sizeof(g_flash));
}

/*---------------------------------------------------------------------------*
 * Name        : void mock_advance_ms(uint32_t ms)
 * Input       : ms - 需要推进的模拟时间，单位ms
 * Output      : 无
 * Description : 推进Host单调毫秒时基，供调度、去抖、超时和看门狗回归测试使用。
 *---------------------------------------------------------------------------*/
void mock_advance_ms(uint32_t ms)
{
    g_ms += ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void mock_set_break(bool active)
 * Input       : active - true表示快速故障源有效；false表示故障源已释放
 * Output      : 无
 * Description : 设置模拟Break实时电平；故障有效时同时锁存Break并立即撤销PWM输出，模拟硬件快速关断行为。
 *---------------------------------------------------------------------------*/
void mock_set_break(bool active)
{
    g_break_source = active;

    if (active) {
        g_break_latched = true;
        g_pwm_active = false;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void mock_apply_uev(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟一次自然UPDATE事件，把最新shadow提交序号发布为已生效序号。
 *---------------------------------------------------------------------------*/
void mock_apply_uev(void)
{
    g_applied_sequence = g_staged_sequence;
}

/*---------------------------------------------------------------------------*
 * Name        : uint16_t mock_duty(void)
 * Input       : 无
 * Output      : 当前暂存的Q15物理占空比
 * Description : 返回Host模拟PWM驱动最近一次接收的占空比，用于断言Duty限幅、斜坡和故障归零行为。
 *---------------------------------------------------------------------------*/
uint16_t mock_duty(void)
{
    return g_duty;
}

/*---------------------------------------------------------------------------*
 * Name        : bool mock_pwm_active(void)
 * Input       : 无
 * Output      : true表示模拟主输出已放行；false表示PWM保持关闭
 * Description : 返回Host模拟PWM输出许可状态。
 *---------------------------------------------------------------------------*/
bool mock_pwm_active(void)
{
    return g_pwm_active;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t mock_watchdog_feeds(void)
 * Input       : 无
 * Output      : 自上次mock_reset以来的看门狗刷新次数
 * Description : 返回看门狗喂狗计数，用于验证只有健康监督路径能够刷新硬件看门狗。
 *---------------------------------------------------------------------------*/
uint32_t mock_watchdog_feeds(void)
{
    return g_watchdog_feeds;
}

/*---------------------------------------------------------------------------*
 * Name        : size_t mock_uart_tx_length(void)
 * Input       : 无
 * Output      : 当前已写入Host模拟UART发送缓存的字节数
 * Description : 返回累计发送长度，用于验证协议帧是否完整入队且未被部分截断。
 *---------------------------------------------------------------------------*/
size_t mock_uart_tx_length(void)
{
    return g_uart_tx_length;
}

/*---------------------------------------------------------------------------*
 * Name        : const uint8_t *mock_uart_tx_data(void)
 * Input       : 无
 * Output      : Host模拟UART累计发送缓存首地址
 * Description : 返回当前发送缓存，供Debug日志测试检查前缀和模块标签。
 *---------------------------------------------------------------------------*/
const uint8_t *mock_uart_tx_data(void)
{
    return g_uart_tx;
}

/*---------------------------------------------------------------------------*
 * Name        : void mock_uart_clear_tx(void)
 * Input       : 无
 * Output      : 无
 * Description : 清空Host模拟UART发送缓存，不改变其他模拟外设状态。
 *---------------------------------------------------------------------------*/
void mock_uart_clear_tx(void)
{
    g_uart_tx_length = 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool mock_relay(void)
 * Input       : 无
 * Output      : true表示继电器控制命令为闭合；false表示命令为断开
 * Description : 返回Host模拟继电器输出状态。
 *---------------------------------------------------------------------------*/
bool mock_relay(void)
{
    return g_relay;
}

/*---------------------------------------------------------------------------*
 * Name        : uint16_t *mock_adc_block(uint8_t index)
 * Input       : index - 模拟DMA半缓冲索引，只允许0或1
 * Output      : 有效索引返回可写采样块指针；无效索引返回NULL
 * Description : 向测试代码暴露指定ADC完成块，以便构造完整扫描数据后再触发Service发布。
 *---------------------------------------------------------------------------*/
uint16_t *mock_adc_block(uint8_t index)
{
    if (index >= MOCK_ADC_BLOCK_COUNT) {
        return NULL;
    }

    return g_adc[index];
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 初始化Host模拟系统时基；目标端对应系统时钟、SysTick和基础异常环境初始化。
 *---------------------------------------------------------------------------*/
void drv_system_init(void)
{
    g_ms = 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_time_now_ms(void)
 * Input       : 无
 * Output      : 当前Host模拟系统毫秒时间戳
 * Description : 返回由测试或模拟SysTick推进的单调毫秒计数。
 *---------------------------------------------------------------------------*/
uint32_t drv_time_now_ms(void)
{
    return g_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_time_tick_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟一次SysTick中断，使系统毫秒计数递增1。
 *---------------------------------------------------------------------------*/
void drv_time_tick_isr(void)
{
    g_ms++;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_irq_state_t drv_irq_save(void)
 * Input       : 无
 * Output      : 模拟的调用前中断状态，Host固定返回0
 * Description : 提供与目标端相同的临界区接口；Host单线程测试不实际屏蔽中断。
 *---------------------------------------------------------------------------*/
aurora_irq_state_t drv_irq_save(void)
{
    return 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_restore(aurora_irq_state_t state)
 * Input       : state - drv_irq_save返回的中断状态
 * Output      : 无
 * Description : 恢复目标端中断状态；Host单线程测试仅保留接口契约。
 *---------------------------------------------------------------------------*/
void drv_irq_restore(aurora_irq_state_t state)
{
    (void)state;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_irq_configure_priorities(void)
 * Input       : 无
 * Output      : 无
 * Description : Host无需配置NVIC；保留空实现以验证Service初始化调用链完整。
 *---------------------------------------------------------------------------*/
void drv_irq_configure_priorities(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_system_reset(void)
 * Input       : 无
 * Output      : 无
 * Description : Host测试不执行真实复位；保留空实现以满足目标驱动接口。
 *---------------------------------------------------------------------------*/
void drv_system_reset(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_adc_init(void)
 * Input       : 无
 * Output      : Host固定返回true，表示模拟ADC/DMA初始化成功
 * Description : 提供目标ADC初始化接口的成功路径；通道数据由mock_adc_block注入。
 *---------------------------------------------------------------------------*/
bool drv_adc_init(void)
{
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_adc_start(void)
 * Input       : 无
 * Output      : Host固定返回true，表示模拟ADC采样链已启动
 * Description : 提供目标端DMA先启用、ADC后武装的启动接口契约。
 *---------------------------------------------------------------------------*/
bool drv_adc_start(void)
{
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : const uint16_t *drv_adc_completed_block(uint8_t block_index)
 * Input       : block_index - DMA完成块索引，只允许0或1
 * Output      : 有效索引返回只读采样块；无效索引返回NULL
 * Description : 返回ISR已发布的完整ADC扫描块，禁止业务层读取正在写入的另一半缓冲。
 *---------------------------------------------------------------------------*/
const uint16_t *drv_adc_completed_block(uint8_t block_index)
{
    if (block_index >= MOCK_ADC_BLOCK_COUNT) {
        return NULL;
    }

    return g_adc[block_index];
}

/*---------------------------------------------------------------------------*
 * Name        : size_t drv_adc_block_words(void)
 * Input       : 无
 * Output      : 单个完整ADC DMA块包含的16位采样字数
 * Description : 返回逻辑通道数乘以每块扫描次数，供Measurement校验块边界。
 *---------------------------------------------------------------------------*/
size_t drv_adc_block_words(void)
{
    return DRV_ADC_BLOCK_WORDS;
}

/*---------------------------------------------------------------------------*
 * Name        : uint8_t drv_adc_dma_irq_ack(void)
 * Input       : 无
 * Output      : Host默认返回0，表示没有待确认的DMA完成或错误事件
 * Description : 保留目标端HT/TC/TE中断应答接口；测试通过Service回调直接发布完成块。
 *---------------------------------------------------------------------------*/
uint8_t drv_adc_dma_irq_ack(void)
{
    return 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_init(void)
 * Input       : 无
 * Output      : Host固定返回true，表示模拟PWM与Break初始化成功
 * Description : 提供50 kHz单路异步Boost PWM初始化契约；mock_reset保证初始输出关闭且Duty为0。
 *---------------------------------------------------------------------------*/
bool drv_pwm_init(void)
{
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_force_off_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟快速故障ISR的恒定时间关波动作，只撤销主输出，不在ISR中执行恢复流程。
 *---------------------------------------------------------------------------*/
void drv_pwm_force_off_isr(void)
{
    g_pwm_active = false;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_quiesce_break_irq_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : Host没有重复Break中断源；保留空实现以匹配目标端屏蔽重复中断的接口。
 *---------------------------------------------------------------------------*/
void drv_pwm_quiesce_break_irq_isr(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_disarm(void)
 * Input       : 无
 * Output      : 无
 * Description : 撤销Host模拟PWM输出许可，使普通控制链失去发波能力。
 *---------------------------------------------------------------------------*/
void drv_pwm_disarm(void)
{
    g_pwm_active = false;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
 * Input       : sequence - 待确认零Duty提交序号输出指针，可为NULL
 * Output      : true表示零占空比已写入shadow并生成提交序号
 * Description : 关波后暂存零Duty，模拟首次arm前必须等待自然UEV确认安全CCR已经生效的步骤。
 *---------------------------------------------------------------------------*/
bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
{
    g_pwm_active = false;
    g_duty = 0U;
    g_staged_sequence++;

    if (sequence != NULL) {
        *sequence = g_staged_sequence;
    }

    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence)
 * Input       : duty_q15 - Q15物理占空比；sequence - 本次提交序号输出指针，可为NULL
 * Output      : true表示占空比已写入shadow并生成提交序号
 * Description : 暂存新的物理Duty但不主动产生UPDATE事件，保持运行期仅由自然UEV提交的安全约束。
 *---------------------------------------------------------------------------*/
bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence)
{
    g_duty = duty_q15;
    g_staged_sequence++;

    if (sequence != NULL) {
        *sequence = g_staged_sequence;
    }

    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_arm(void)
 * Input       : 无
 * Output      : true表示PWM已安全放行；false表示实时Break或锁存仍有效
 * Description : 仅在实时故障源和Break锁存均清除时放行Host模拟主输出，模拟目标端arm前后的故障复核。
 *---------------------------------------------------------------------------*/
bool drv_pwm_arm(void)
{
    if (g_break_source || g_break_latched) {
        return false;
    }

    g_pwm_active = true;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_output_active(void)
 * Input       : 无
 * Output      : true表示Host模拟主输出已使能；false表示输出关闭
 * Description : 返回当前PWM放行状态，供Flash门禁和Service健康检查使用。
 *---------------------------------------------------------------------------*/
bool drv_pwm_output_active(void)
{
    return g_pwm_active;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_break_source_active(void)
 * Input       : 无
 * Output      : true表示至少一路模拟快速故障源仍有效；false表示实时故障源已释放
 * Description : 返回Host模拟比较器到Break输入的实时故障电平。
 *---------------------------------------------------------------------------*/
bool drv_pwm_break_source_active(void)
{
    return g_break_source;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_break_latched(void)
 * Input       : 无
 * Output      : true表示Break故障已锁存；false表示锁存已清除
 * Description : 返回Host模拟ATMR Break锁存状态。
 *---------------------------------------------------------------------------*/
bool drv_pwm_break_latched(void)
{
    return g_break_latched;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_pwm_clear_break_latch(void)
 * Input       : 无
 * Output      : true表示锁存已清除；false表示输出仍在运行或实时故障源尚未释放
 * Description : 只有在PWM关闭且Break实时源无效时才允许清除锁存，避免故障仍存在时提前恢复。
 *---------------------------------------------------------------------------*/
bool drv_pwm_clear_break_latch(void)
{
    if (g_break_source || g_pwm_active) {
        return false;
    }

    g_break_latched = false;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_pwm_applied_sequence(void)
 * Input       : 无
 * Output      : 最近一次已由模拟自然UEV确认生效的提交序号
 * Description : 返回active CCR对应的提交代次，供Service判断首次零Duty是否真正生效。
 *---------------------------------------------------------------------------*/
uint32_t drv_pwm_applied_sequence(void)
{
    return g_applied_sequence;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_pwm_update_isr_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : 模拟一次UPDATE ISR应答，把shadow提交序号发布为已生效序号。
 *---------------------------------------------------------------------------*/
void drv_pwm_update_isr_ack(void)
{
    mock_apply_uev();
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_comp_init(void)
 * Input       : 无
 * Output      : Host固定返回true，表示模拟OPA/COMP快速保护链初始化成功
 * Description : 保留目标比较器初始化接口；实时故障电平由mock_set_break控制。
 *---------------------------------------------------------------------------*/
bool drv_comp_init(void)
{
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t drv_comp_fault_mask(void)
 * Input       : 无
 * Output      : DRV_FAULT_*快速故障位图；无故障时返回0
 * Description : 把Host模拟Break实时源映射为MOS快速过流故障位，供ISR邮箱发布。
 *---------------------------------------------------------------------------*/
uint32_t drv_comp_fault_mask(void)
{
    return g_break_source ? DRV_FAULT_MOS_OCP : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_comp_irq_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : Host没有真实比较器中断标志；保留空实现以满足ISR调用契约。
 *---------------------------------------------------------------------------*/
void drv_comp_irq_ack(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_init(void)
 * Input       : 无
 * Output      : 无
 * Description : Host输出状态已由mock_reset建立；保留目标GPIO安全态初始化接口。
 *---------------------------------------------------------------------------*/
void drv_io_init(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_relay(bool on)
 * Input       : on - true表示请求继电器闭合；false表示请求断开
 * Output      : 无
 * Description : 更新Host模拟继电器控制状态。
 *---------------------------------------------------------------------------*/
void drv_io_set_relay(bool on)
{
    g_relay = on;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_link(bool on)
 * Input       : on - true表示LINK有效；false表示LINK无效
 * Output      : 无
 * Description : 更新Host模拟LINK控制状态。
 *---------------------------------------------------------------------------*/
void drv_io_set_link(bool on)
{
    g_link = on;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_leds(bool run_on, bool fault_on)
 * Input       : run_on - RUN灯逻辑点亮命令；fault_on - FAULT灯逻辑点亮命令
 * Output      : 无
 * Description : 更新Host模拟RUN和FAULT指示灯状态，测试关注逻辑语义而非目标端低有效电平。
 *---------------------------------------------------------------------------*/
void drv_io_set_leds(bool run_on, bool fault_on)
{
    g_run_led = run_on;
    g_fault_led = fault_on;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_init(void)
 * Input       : 无
 * Output      : Host固定返回true，表示模拟产品UART初始化成功
 * Description : 保留目标USART初始化接口；发送字节累计在Host缓存中供协议测试检查。
 *---------------------------------------------------------------------------*/
bool drv_uart_init(void)
{
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_send(const uint8_t *data, size_t length)
 * Input       : data - 待发送帧缓冲区；length - 帧长度
 * Output      : true表示整帧已写入模拟发送缓存；false表示空指针、长度溢出或剩余空间不足
 * Description : 只接受完整帧原子入队，禁止缓冲不足时出现部分帧，便于验证协议发送契约。
 *---------------------------------------------------------------------------*/
bool drv_uart_send(const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length > (sizeof(g_uart_tx) - g_uart_tx_length))) {
        return false;
    }

    memcpy(&g_uart_tx[g_uart_tx_length], data, length);
    g_uart_tx_length += length;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_tx_busy(void)
 * Input       : 无
 * Output      : Host固定返回false，表示模拟发送立即完成
 * Description : Host不模拟逐字节TXE节拍；完整帧写入缓存后即视为发送链空闲。
 *---------------------------------------------------------------------------*/
bool drv_uart_tx_busy(void)
{
    return false;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_rx_ready_isr(void)
 * Input       : 无
 * Output      : Host固定返回false，表示没有模拟RX字节待取
 * Description : 当前Host测试通过Service接口直接注入接收字节，不使用真实USART状态寄存器。
 *---------------------------------------------------------------------------*/
bool drv_uart_rx_ready_isr(void)
{
    return false;
}

/*---------------------------------------------------------------------------*
 * Name        : uint8_t drv_uart_read_isr(void)
 * Input       : 无
 * Output      : Host固定返回0，表示默认接收字节值
 * Description : 保留目标端USART接收寄存器读取接口；正常Host测试不直接调用该默认路径。
 *---------------------------------------------------------------------------*/
uint8_t drv_uart_read_isr(void)
{
    return 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_uart_tx_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : Host不模拟TXE逐字节搬运；保留空实现以匹配目标ISR入口。
 *---------------------------------------------------------------------------*/
void drv_uart_tx_isr(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_uart_irq_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : Host没有USART错误标志；保留空实现以满足目标端中断应答接口。
 *---------------------------------------------------------------------------*/
void drv_uart_irq_ack(void)
{
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_read(uint32_t address, void *data, size_t length)
 * Input       : address - 参数保留区内的目标地址；data - 输出缓冲区；length - 读取长度
 * Output      : true表示读取完成；false表示缓冲区为空或地址区间非法
 * Description : 从Host模拟Flash读取数据，并复用统一范围检查阻止越界访问。
 *---------------------------------------------------------------------------*/
bool drv_flash_read(uint32_t address, void *data, size_t length)
{
    size_t offset;

    if ((data == NULL) || !flash_range(address, length, &offset)) {
        return false;
    }

    memcpy(data, &g_flash[offset], length);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_erase_page(uint32_t address)
 * Input       : address - 待擦除页首地址
 * Output      : true表示整页已恢复为0xFF；false表示PWM仍在运行、地址越界或未按物理页对齐
 * Description : 模拟目标端Flash页擦除门禁，功率输出有效时绝不允许修改NVM。
 *---------------------------------------------------------------------------*/
bool drv_flash_erase_page(uint32_t address)
{
    size_t offset;

    if (g_pwm_active ||
        !flash_range(address, MOCK_FLASH_PAGE_SIZE_BYTES, &offset) ||
        ((offset % MOCK_FLASH_PAGE_SIZE_BYTES) != 0U)) {
        return false;
    }

    memset(&g_flash[offset], 0xFF, MOCK_FLASH_PAGE_SIZE_BYTES);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_program(uint32_t address, const void *data, size_t length)
 * Input       : address - 起始编程地址；data - 待写入数据；length - 编程长度
 * Output      : true表示编程完成；false表示参数非法、地址越界或PWM仍在运行
 * Description : 使用按位与模拟Flash只能从1写成0的物理特性，并保持功率运行期间禁止写Flash的门禁。
 *---------------------------------------------------------------------------*/
bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    size_t offset;
    size_t i;

    if ((data == NULL) || g_pwm_active || !flash_range(address, length, &offset)) {
        return false;
    }

    for (i = 0U; i < length; ++i) {
        g_flash[offset + i] &= ((const uint8_t *)data)[i];
    }

    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_watchdog_init(uint32_t timeout_ms)
 * Input       : timeout_ms - 期望看门狗超时时间，单位ms
 * Output      : timeout_ms非0时返回true；为0时返回false
 * Description : Host只验证参数契约，不模拟LSI误差、预分频或真实复位延迟。
 *---------------------------------------------------------------------------*/
bool drv_watchdog_init(uint32_t timeout_ms)
{
    return timeout_ms != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_watchdog_feed(void)
 * Input       : 无
 * Output      : 无
 * Description : 累加Host喂狗次数，用于断言只有Service健康监督能够刷新看门狗。
 *---------------------------------------------------------------------------*/
void drv_watchdog_feed(void)
{
    g_watchdog_feeds++;
}
