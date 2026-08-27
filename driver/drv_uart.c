#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"
#include "g32f031_ddl_usart.h"

#define UART_TX_BUFFER_SIZE (256U)

static uint8_t g_tx[UART_TX_BUFFER_SIZE];
static volatile uint16_t g_tx_head;
static volatile uint16_t g_tx_tail;

bool drv_uart_init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0U};
    DDL_USART_InitTypeDef usart = {0U};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_USART);
    DDL_RCC_Lock();

    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Pull = DDL_GPIO_PULL_UP;
    gpio.Alternate = DDL_GPIO_AF_0;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DDL_GPIO_PIN_10; /* UR_TX / PA10 / AF0 */
    DDL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = DDL_GPIO_PIN_11; /* UR_RX / PA11 / AF0 */
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);

    usart.BaudRate = BOARD_UART_BAUDRATE;
    usart.DataWidth = DDL_USART_DATAWIDTH_8B;
    usart.StopBits = DDL_USART_STOPBITS_1;
    usart.Parity = DDL_USART_PARITY_NONE;
    usart.TransferDirection = DDL_USART_DIRECTION_TX_RX;
    usart.HardwareFlowControl = DDL_USART_HWCONTROL_NONE;
    usart.OverSampling = DDL_USART_OVERSAMPLING_16;
    if (DDL_USART_Init(USART, &usart) != SUCCESS)
    {
        return false;
    }

    g_tx_head = 0U;
    g_tx_tail = 0U;
    DDL_USART_EnableIT_RXNE(USART);
    DDL_USART_Enable(USART);
    NVIC_EnableIRQ(USART_IRQn);
    return true;
}

bool drv_uart_send(const uint8_t *data, size_t length)
{
    size_t i;
    aurora_irq_state_t irq;

    if ((data == NULL) || (length >= UART_TX_BUFFER_SIZE))
    {
        return false;
    }

    irq = drv_irq_save();
    {
        const uint16_t used = (g_tx_head >= g_tx_tail) ?
                                  (uint16_t)(g_tx_head - g_tx_tail) :
                                  (uint16_t)(UART_TX_BUFFER_SIZE - g_tx_tail + g_tx_head);
        const uint16_t free_bytes = (uint16_t)(UART_TX_BUFFER_SIZE - 1U - used);
        if (length > free_bytes)
        {
            /* 一帧必须整体入队，绝不留下无法解析的半帧。 */
            drv_irq_restore(irq);
            return false;
        }
    }
    for (i = 0U; i < length; ++i)
    {
        g_tx[g_tx_head] = data[i];
        g_tx_head = (uint16_t)((g_tx_head + 1U) % UART_TX_BUFFER_SIZE);
    }
    DDL_USART_EnableIT_TXE(USART);
    drv_irq_restore(irq);
    return true;
}

bool drv_uart_tx_busy(void)
{
    return g_tx_head != g_tx_tail;
}

bool drv_uart_rx_ready_isr(void)
{
    return DDL_USART_IsActiveFlag_RXNE(USART) != 0U;
}

uint8_t drv_uart_read_isr(void)
{
    return DDL_USART_ReceiveData8(USART);
}

void drv_uart_tx_isr(void)
{
    if ((DDL_USART_IsActiveFlag_TXE(USART) != 0U) && (g_tx_tail != g_tx_head))
    {
        DDL_USART_TransmitData8(USART, g_tx[g_tx_tail]);
        g_tx_tail = (uint16_t)((g_tx_tail + 1U) % UART_TX_BUFFER_SIZE);
    }
    if (g_tx_tail == g_tx_head)
    {
        DDL_USART_DisableIT_TXE(USART);
    }
}

void drv_uart_irq_ack(void)
{
    if (DDL_USART_IsActiveFlag_ORE(USART) != 0U)
    {
        DDL_USART_ClearFlag_ORE(USART);
    }
    if (DDL_USART_IsActiveFlag_NE(USART) != 0U)
    {
        DDL_USART_ClearFlag_NE(USART);
    }
    if (DDL_USART_IsActiveFlag_FE(USART) != 0U)
    {
        DDL_USART_ClearFlag_FE(USART);
    }
}
