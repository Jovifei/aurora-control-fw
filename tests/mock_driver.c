#include "driver.h"
#include "mock_driver.h"

#include <string.h>

static uint32_t g_ms;
static uint16_t g_adc[2][DRV_ADC_BLOCK_WORDS];
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
static uint8_t g_uart_tx[512];
static size_t g_uart_tx_length;
static uint8_t g_flash[1024];

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
void mock_advance_ms(uint32_t ms) { g_ms += ms; }
void mock_set_break(bool active) { g_break_source = active; if (active) { g_break_latched = true; g_pwm_active = false; } }
void mock_apply_uev(void) { g_applied_sequence = g_staged_sequence; }
uint16_t mock_duty(void) { return g_duty; }
bool mock_pwm_active(void) { return g_pwm_active; }
uint32_t mock_watchdog_feeds(void) { return g_watchdog_feeds; }
size_t mock_uart_tx_length(void) { return g_uart_tx_length; }
bool mock_relay(void) { return g_relay; }
uint16_t *mock_adc_block(uint8_t index) { return (index < 2U) ? g_adc[index] : NULL; }

void drv_system_init(void) { g_ms = 0U; }
uint32_t drv_time_now_ms(void) { return g_ms; }
void drv_time_tick_isr(void) { g_ms++; }
aurora_irq_state_t drv_irq_save(void) { return 0U; }
void drv_irq_restore(aurora_irq_state_t state) { (void)state; }
void drv_irq_configure_priorities(void) {}
void drv_system_reset(void) {}
bool drv_adc_init(void) { return true; }
bool drv_adc_start(void) { return true; }
const uint16_t *drv_adc_completed_block(uint8_t block_index) { return (block_index < 2U) ? g_adc[block_index] : NULL; }
size_t drv_adc_block_words(void) { return DRV_ADC_BLOCK_WORDS; }
uint8_t drv_adc_dma_irq_ack(void) { return 0U; }
bool drv_pwm_init(void) { return true; }
void drv_pwm_force_off_isr(void) { g_pwm_active = false; }
void drv_pwm_quiesce_break_irq_isr(void) {}
void drv_pwm_disarm(void) { g_pwm_active = false; }
bool drv_pwm_prepare_arm_zero(uint32_t *sequence)
{
    g_pwm_active = false;
    g_duty = 0U;
    g_staged_sequence++;
    if (sequence != NULL) { *sequence = g_staged_sequence; }
    return true;
}
bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence)
{
    g_duty = duty_q15;
    g_staged_sequence++;
    if (sequence != NULL) { *sequence = g_staged_sequence; }
    return true;
}
bool drv_pwm_arm(void)
{
    if (g_break_source || g_break_latched) { return false; }
    g_pwm_active = true;
    return true;
}
bool drv_pwm_output_active(void) { return g_pwm_active; }
bool drv_pwm_break_source_active(void) { return g_break_source; }
bool drv_pwm_break_latched(void) { return g_break_latched; }
bool drv_pwm_clear_break_latch(void)
{
    if (g_break_source || g_pwm_active) { return false; }
    g_break_latched = false;
    return true;
}
uint32_t drv_pwm_applied_sequence(void) { return g_applied_sequence; }
void drv_pwm_update_isr_ack(void) { mock_apply_uev(); }
bool drv_comp_init(void) { return true; }
uint32_t drv_comp_fault_mask(void) { return g_break_source ? DRV_FAULT_MOS_OCP : 0U; }
void drv_comp_irq_ack(void) {}
void drv_io_init(void) {}
void drv_io_set_relay(bool on) { g_relay = on; }
void drv_io_set_link(bool on) { g_link = on; }
void drv_io_set_leds(bool run_on, bool fault_on) { g_run_led = run_on; g_fault_led = fault_on; }
bool drv_uart_init(void) { return true; }
bool drv_uart_send(const uint8_t *data, size_t length)
{
    if ((data == NULL) || ((g_uart_tx_length + length) > sizeof(g_uart_tx))) { return false; }
    memcpy(&g_uart_tx[g_uart_tx_length], data, length);
    g_uart_tx_length += length;
    return true;
}
bool drv_uart_tx_busy(void) { return false; }
bool drv_uart_rx_ready_isr(void) { return false; }
uint8_t drv_uart_read_isr(void) { return 0U; }
void drv_uart_tx_isr(void) {}
void drv_uart_irq_ack(void) {}
static bool flash_range(uint32_t address, size_t length, size_t *offset)
{
    if ((address < 0x0000FC00UL) || ((uint64_t)address + length > 0x00010000ULL)) { return false; }
    *offset = (size_t)(address - 0x0000FC00UL);
    return true;
}
bool drv_flash_read(uint32_t address, void *data, size_t length)
{
    size_t offset;
    if ((data == NULL) || !flash_range(address, length, &offset)) { return false; }
    memcpy(data, &g_flash[offset], length);
    return true;
}
bool drv_flash_erase_page(uint32_t address)
{
    size_t offset;
    if (g_pwm_active || !flash_range(address, 512U, &offset) || ((offset % 512U) != 0U)) { return false; }
    memset(&g_flash[offset], 0xFF, 512U);
    return true;
}
bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    size_t offset;
    size_t i;
    if ((data == NULL) || g_pwm_active || !flash_range(address, length, &offset)) { return false; }
    for (i = 0U; i < length; ++i) { g_flash[offset + i] &= ((const uint8_t *)data)[i]; }
    return true;
}
bool drv_watchdog_init(uint32_t timeout_ms) { return timeout_ms != 0U; }
void drv_watchdog_feed(void) { g_watchdog_feeds++; }
