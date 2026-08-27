#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"

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

    /* 调试串口当前不参与产品逻辑；TX保持空闲高，RX保持复位输入态。 */
    gpio_output(GPIOB, DDL_GPIO_PIN_7, true);   /* DEBUG_TX */
}

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
