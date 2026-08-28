#include "drv_uart.h"

#include "board_config.h"
#include "drv_system.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"
#include "g32f031_ddl_usart.h"

static uint8_t g_tx[BOARD_UART_TX_BUFFER_SIZE];
static volatile uint16_t g_tx_head;
static volatile uint16_t g_tx_tail;

/* 把板级配置引脚编号转换为DDL GPIO位掩码，避免驱动再次复制PinMap数值。 */
#define DRV_UART_GPIO_PIN(number)                   (1UL << (number))

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_init(void)
 * Input       : 无
 * Output      : true表示GPIO、USART和RX中断初始化成功；false表示DDL初始化失败
 * Description : 按BOARD_USART_MODE配置蓝牙PA10/PA11或Debug PB7/PB8，初始化115200 8N1和收发缓冲。
 *---------------------------------------------------------------------------*/
bool drv_uart_init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0U};
    DDL_USART_InitTypeDef usart = {0U};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA |
                              DDL_AHB_GRP1_PERIPH_GPIOB);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_USART);
    DDL_RCC_Lock();

    gpio.Mode = DDL_GPIO_MODE_ALTERNATE;
    gpio.Drive = DDL_GPIO_DRIVE_HIGH;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Pull = DDL_GPIO_PULL_UP;
    gpio.Alternate = DDL_GPIO_AF_0;
#if (BOARD_USART_MODE == BOARD_USART_MODE_BLUETOOTH)
#if (BOARD_PIN_UART_TX_PORT != 'A') || (BOARD_PIN_UART_RX_PORT != 'A')
#error "Bluetooth USART pins must remain on GPIOA"
#endif
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DRV_UART_GPIO_PIN(BOARD_PIN_UART_TX_NUMBER); /* UR_TX -> BLE_RX */
    DDL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = DRV_UART_GPIO_PIN(BOARD_PIN_UART_RX_NUMBER); /* BLE_TX -> UR_RX */
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);
#else
#if (BOARD_PIN_DEBUG_TX_PORT != 'B') || (BOARD_PIN_DEBUG_RX_PORT != 'B')
#error "Debug USART pins must remain on GPIOB"
#endif
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_DISABLE);
    gpio.Pin = DRV_UART_GPIO_PIN(BOARD_PIN_DEBUG_TX_NUMBER); /* DEBUG_TX / USART / AF0 */
    DDL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = DRV_UART_GPIO_PIN(BOARD_PIN_DEBUG_RX_NUMBER); /* DEBUG_RX / USART / AF0 */
    DDL_GPIO_Init(GPIOB, &gpio);
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_ENABLE);
#endif

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
#if (BOARD_USART_MODE == BOARD_USART_MODE_BLUETOOTH)
    DDL_USART_EnableIT_RXNE(USART);
#else
    /* Debug路由只输出日志，关闭RX中断防止调试输入进入产品协议。 */
    DDL_USART_DisableIT_RXNE(USART);
#endif
    DDL_USART_Enable(USART);
    NVIC_EnableIRQ(USART_IRQn);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_send(const uint8_t *data, size_t length)
 * Input       : data - 数据缓冲区；length - 数据长度
 * Output      : true表示完整帧已入队，false表示参数错误或空间不足
 * Description : 在短临界区内把完整帧原子写入发送环形缓冲；空间不足时整帧拒绝。
 *---------------------------------------------------------------------------*/
bool drv_uart_send(const uint8_t *data, size_t length)
{
    size_t i;
    aurora_irq_state_t irq;

    if ((data == NULL) || (length >= BOARD_UART_TX_BUFFER_SIZE))
    {
        return false;
    }

    irq = drv_irq_save();
    {
        const uint16_t used = (g_tx_head >= g_tx_tail) ?
                                  (uint16_t)(g_tx_head - g_tx_tail) :
                                  (uint16_t)(BOARD_UART_TX_BUFFER_SIZE - g_tx_tail + g_tx_head);
        const uint16_t free_bytes = (uint16_t)(BOARD_UART_TX_BUFFER_SIZE - 1U - used);
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
        g_tx_head = (uint16_t)((g_tx_head + 1U) % BOARD_UART_TX_BUFFER_SIZE);
    }
    DDL_USART_EnableIT_TXE(USART);
    drv_irq_restore(irq);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_tx_busy(void)
 * Input       : 无
 * Output      : true表示仍有数据待发送
 * Description : 判断发送环形缓冲中是否仍有待发送字节。
 *---------------------------------------------------------------------------*/
bool drv_uart_tx_busy(void)
{
    return g_tx_head != g_tx_tail;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_uart_rx_ready_isr(void)
 * Input       : 无
 * Output      : true表示RX寄存器有数据
 * Description : 在USART ISR中读取RXNE状态。
 *---------------------------------------------------------------------------*/
bool drv_uart_rx_ready_isr(void)
{
    return DDL_USART_IsActiveFlag_RXNE(USART) != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : uint8_t drv_uart_read_isr(void)
 * Input       : 无
 * Output      : 读取到的一个字节
 * Description : 从USART接收寄存器读取一个字节。
 *---------------------------------------------------------------------------*/
uint8_t drv_uart_read_isr(void)
{
    return DDL_USART_ReceiveData8(USART);
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_uart_tx_isr(void)
 * Input       : 无
 * Output      : 无
 * Description : 在TXE有效时发送一个环形缓冲字节，发送完毕后关闭TXE中断。
 *---------------------------------------------------------------------------*/
void drv_uart_tx_isr(void)
{
    if ((DDL_USART_IsActiveFlag_TXE(USART) != 0U) && (g_tx_tail != g_tx_head))
    {
        DDL_USART_TransmitData8(USART, g_tx[g_tx_tail]);
        g_tx_tail = (uint16_t)((g_tx_tail + 1U) % BOARD_UART_TX_BUFFER_SIZE);
    }
    if (g_tx_tail == g_tx_head)
    {
        DDL_USART_DisableIT_TXE(USART);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void drv_uart_irq_ack(void)
 * Input       : 无
 * Output      : 无
 * Description : 清除USART溢出、噪声和帧错误标志。
 *---------------------------------------------------------------------------*/
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
