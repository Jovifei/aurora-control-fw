#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"

/* 把board层引脚编号转换为DDL GPIO位掩码，避免IO安全态复制PinMap数值。 */
#define DRV_IO_GPIO_PIN(number)                    (1UL << (number))

/*---------------------------------------------------------------------------*
 * Name        : static void gpio_output(GPIO_TypeDef *port, uint32_t pin, bool high)
 * Input       : port - GPIO端口；pin - GPIO引脚位图；high - 期望输出电平
 * Output      : 无
 * Description : 配置推挽输出并在锁定引脚前写入指定安全电平。
 *---------------------------------------------------------------------------*/
static void gpio_output(GPIO_TypeDef *port, uint32_t pin, bool high)
{
    DDL_GPIO_InitTypeDef config = {0U};
    config.Pin = pin;
    config.Mode = DDL_GPIO_MODE_OUTPUT;
    config.Drive = DDL_GPIO_DRIVE_HIGH;
    config.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    config.InputEnable = DDL_GPIO_INPUT_DISABLE;
    config.Pull = DDL_GPIO_PULL_NO;
    config.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(port, &config);
    if (high)
    {
        DDL_GPIO_SetOutputPin(port, pin);
    }
    else
    {
        DDL_GPIO_ResetOutputPin(port, pin);
    }
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_ENABLE);
}

/*---------------------------------------------------------------------------*
 * Name        : static void gpio_input(GPIO_TypeDef *port, uint32_t pin)
 * Input       : port - GPIO端口；pin - GPIO引脚位图
 * Output      : 无
 * Description : 配置未选中USART路由的输入安全态，避免复用切换前引脚悬空。
 *---------------------------------------------------------------------------*/
static void gpio_input(GPIO_TypeDef *port, uint32_t pin)
{
    DDL_GPIO_InitTypeDef config = {0U};
    config.Pin = pin;
    config.Mode = DDL_GPIO_MODE_INPUT;
    config.Drive = DDL_GPIO_DRIVE_LOW;
    config.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    config.InputEnable = DDL_GPIO_INPUT_ENABLE;
    config.Pull = DDL_GPIO_PULL_NO;
    config.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(port, &config);
    DDL_GPIO_LockKey(port, DDL_GPIO_LOCK_ENABLE);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_init(void)
 * Input       : 无
 * Output      : 无
 * Description : 优先建立功率GPIO安全态，再初始化继电器、LINK、LED和两组USART路由安全电平。
 *---------------------------------------------------------------------------*/
void drv_io_init(void)
{
    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA | DDL_AHB_GRP1_PERIPH_GPIOB);
    DDL_RCC_Lock();

    /*
     * 功率相关GPIO必须先于定时器/比较器初始化进入安全态。
     * PA15稍后才切换到ATMR复用，杜绝外设初始化期间出现未知脉冲。
     */
    gpio_output(GPIOA, DDL_GPIO_PIN_15, false); /* GLC：默认低，Q1/Q2关闭 */
    gpio_output(GPIOA, DDL_GPIO_PIN_14, false); /* GHC：本板未使用，永久低 */
    gpio_output(GPIOA, DDL_GPIO_PIN_13, false); /* RELAY：默认断开 */
    gpio_output(GPIOA, DDL_GPIO_PIN_12, false); /* LINK：默认关闭 */

    /* 最新图纸两颗LED均为低电平点亮，启动时先灭。 */
    gpio_output(GPIOB, DDL_GPIO_PIN_9, true);   /* LED_RUN */
    gpio_output(GPIOB, DDL_GPIO_PIN_11, true);  /* LED_FAULT */

    /* 未选中的USART路由保持安全态；drv_uart_init随后只切换当前选择的一组复用。 */
#if (BOARD_PIN_DEBUG_TX_PORT != 'B') || (BOARD_PIN_DEBUG_RX_PORT != 'B')
#error "Debug IO pins must remain on GPIOB"
#endif
    gpio_output(GPIOB, DRV_IO_GPIO_PIN(BOARD_PIN_DEBUG_TX_NUMBER), true); /* DEBUG_TX空闲高 */
    gpio_input(GPIOB, DRV_IO_GPIO_PIN(BOARD_PIN_DEBUG_RX_NUMBER));  /* DEBUG_RX输入态 */
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_relay(bool on)
 * Input       : on - 开关命令
 * Output      : 无
 * Description : 按板级有效电平控制继电器输出。
 *---------------------------------------------------------------------------*/
void drv_io_set_relay(bool on)
{
    if (on == (BOARD_RELAY_ACTIVE_HIGH != 0U))
    {
        DDL_GPIO_SetOutputPin(GPIOA, DDL_GPIO_PIN_13);
    }
    else
    {
        DDL_GPIO_ResetOutputPin(GPIOA, DDL_GPIO_PIN_13);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_link(bool on)
 * Input       : on - 开关命令
 * Output      : 无
 * Description : 设置LINK控制输出。
 *---------------------------------------------------------------------------*/
void drv_io_set_link(bool on)
{
    if (on)
    {
        DDL_GPIO_SetOutputPin(GPIOA, DDL_GPIO_PIN_12);
    }
    else
    {
        DDL_GPIO_ResetOutputPin(GPIOA, DDL_GPIO_PIN_12);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_io_set_leds(bool run_on, bool fault_on)
 * Input       : run_on - RUN灯点亮命令；fault_on - FAULT灯点亮命令
 * Output      : 无
 * Description : 按低有效硬件极性更新RUN和FAULT指示灯。
 *---------------------------------------------------------------------------*/
void drv_io_set_leds(bool run_on, bool fault_on)
{
    if (run_on == (BOARD_LED_ACTIVE_LOW == 0U))
    {
        DDL_GPIO_SetOutputPin(GPIOB, DDL_GPIO_PIN_9);
    }
    else
    {
        DDL_GPIO_ResetOutputPin(GPIOB, DDL_GPIO_PIN_9);
    }

    if (fault_on == (BOARD_LED_ACTIVE_LOW == 0U))
    {
        DDL_GPIO_SetOutputPin(GPIOB, DDL_GPIO_PIN_11);
    }
    else
    {
        DDL_GPIO_ResetOutputPin(GPIOB, DDL_GPIO_PIN_11);
    }
}
