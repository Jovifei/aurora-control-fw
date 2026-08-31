#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_adc.h"
#include "g32f031_ddl_dma.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_rcc.h"

static uint16_t g_adc_dma[2][DRV_ADC_BLOCK_WORDS];

/*---------------------------------------------------------------------------*
 * Name        : static void configure_analog_pin(GPIO_TypeDef *port, uint32_t pin)
 * Input       : port - GPIO端口；pin - GPIO引脚位图
 * Output      : 无
 * Description : 把指定GPIO配置为模拟输入，关闭上下拉并锁定配置。
 *---------------------------------------------------------------------------*/
static void configure_analog_pin(GPIO_TypeDef *port, uint32_t pin)
{
    DDL_GPIO_InitTypeDef config = {0U};
    config.Pin = pin;
    config.Mode = DDL_GPIO_MODE_ANALOG;
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
 * Name        : bool drv_adc_init(void)
 * Input       : 无
 * Output      : true表示ADC规则组、DMA和中断初始化成功；false表示关键配置失败
 * Description :
 * 配置六通道ADC规则序列、ATMR中点触发、DMA双半缓冲和HT/TC/TE中断；任一关键初始化失败即返回。
 *---------------------------------------------------------------------------*/
bool drv_adc_init(void)
{
    DDL_ADC_InitTypeDef adc = {0U};
    DDL_ADC_REG_InitTypeDef regular = {0U};
    DDL_DMA_InitTypeDef dma = {0U};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA | DDL_AHB_GRP1_PERIPH_GPIOB |
                             DDL_AHB_GRP1_PERIPH_DMA);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_ADC);
    DDL_RCC_SetADCClkDiv(DDL_RCC_ADCCLK_DIVISION_4);
    DDL_RCC_Lock();

    configure_analog_pin(GPIOA, DDL_GPIO_PIN_8 | DDL_GPIO_PIN_9);
    configure_analog_pin(GPIOB, DDL_GPIO_PIN_0 | DDL_GPIO_PIN_1 | DDL_GPIO_PIN_5 | DDL_GPIO_PIN_12);

    DDL_ADC_Enable(ADC);
    {
        uint32_t timeout = BOARD_ADC_READY_TIMEOUT_LOOPS;
        while (!DDL_ADC_IsActiveFlag_RDY(ADC) && (timeout > 0U))
        {
            timeout--;
        }
        if (timeout == 0U)
        {
            return false;
        }
    }

    adc.DataAlignment = DDL_ADC_ALIGNMENT_RIGHT;
    if (DDL_ADC_Init(ADC, &adc) != SUCCESS)
    {
        return false;
    }

    regular.TriggerSource = DDL_ADC_REG_TRIG_EXTSEL_ATMR_TRGO0;
    regular.TriggerEdge = DDL_ADC_REG_TRIG_EXTEDGE_FALLING;
    regular.SequencerLength = DDL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS;
    regular.SequencerDiscont = DDL_ADC_REG_SEQ_DISCONT_DISABLE;
    regular.ContinuousMode = DDL_ADC_REG_CONV_SINGLE;
    regular.DMATransfer = DDL_ADC_REG_DMA_TRANSFER_CIRCULAR_MODE;
    regular.Overrun = DDL_ADC_OVERMODE_KEEP;
    if (DDL_ADC_REG_Init(ADC, &regular) != SUCCESS)
    {
        return false;
    }

    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_1, DDL_ADC_CHANNEL_1);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_2, DDL_ADC_CHANNEL_2);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_3, DDL_ADC_CHANNEL_3);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_4, DDL_ADC_CHANNEL_4);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_5, DDL_ADC_CHANNEL_5);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_6, DDL_ADC_CHANNEL_6);
    /*
     * 六通道必须在20us PWM周期内完成。ATMR_CH3在周期中点产生下降沿，
     * 前四路使用8周期采样、两路NTC使用16周期采样；最终用示波器/ADC OVR
     * 计数确认转换预算，禁止恢复成32/64周期后仍按50kHz触发。
     */
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_1, DDL_ADC_SAMPLINGTIME_8_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_2, DDL_ADC_SAMPLINGTIME_8_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_3, DDL_ADC_SAMPLINGTIME_8_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_4, DDL_ADC_SAMPLINGTIME_8_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_5, DDL_ADC_SAMPLINGTIME_16_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_6, DDL_ADC_SAMPLINGTIME_16_SCYCLES);

    dma.Peripheral = DDL_DMA_PERIPHERAL_0;
    dma.Direction = DDL_DMA_DIRECTION_PERIPH_TO_MEMORY;
    dma.PeriphOrM2MSrcAddress = (uint32_t)&ADC->DR;
    dma.PeriphOrM2MSrcDataSize = DDL_DMA_PDATAALIGN_HALFWORD;
    dma.PeriphOrM2MSrcIncMode = DDL_DMA_PERIPH_NOINCREMENT;
    dma.MemoryOrM2MDstAddress = (uint32_t)g_adc_dma;
    dma.MemoryOrM2MDstDataSize = DDL_DMA_MDATAALIGN_HALFWORD;
    dma.MemoryOrM2MDstIncMode = DDL_DMA_MEMORY_INCREMENT;
    dma.NbData = (uint32_t)(2U * DRV_ADC_BLOCK_WORDS);
    dma.Mode = DDL_DMA_MODE_CIRCULAR;
    dma.Priority = DDL_DMA_PRIORITY_HIGH;
    dma.FIFOMode = DDL_DMA_FIFOMODE_DISABLE;
    DDL_DMA_DisableChannel(DMA, DDL_DMA_CHANNEL_1);
    if (DDL_DMA_Init(DMA, DDL_DMA_CHANNEL_1, &dma) != SUCCESS)
    {
        return false;
    }
    DMA->IFCLR = 0xFFFFFFFFUL;
    DDL_DMA_EnableIT_HT(DMA, DDL_DMA_CHANNEL_1);
    DDL_DMA_EnableIT_TC(DMA, DDL_DMA_CHANNEL_1);
    DDL_DMA_EnableIT_TE(DMA, DDL_DMA_CHANNEL_1);
    NVIC_EnableIRQ(DMA_CH1_IRQn);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_adc_start(void)
 * Input       : 无
 * Output      : true表示DMA通道已使能且ADC规则组已武装
 * Description : 先使能DMA，再武装ADC规则组等待ATMR外部触发。
 *---------------------------------------------------------------------------*/
bool drv_adc_start(void)
{
    DDL_DMA_EnableChannel(DMA, DDL_DMA_CHANNEL_1);
    if (DDL_DMA_IsEnabledChannel(DMA, DDL_DMA_CHANNEL_1) == 0U)
    {
        return false;
    }

    /* 外部触发模式也必须先StartConversion，作用是武装规则组等待ATMR TRGO。 */
    DDL_ADC_StartConversion(ADC);
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : const uint16_t *drv_adc_completed_block(uint8_t block_index)
 * Input       : block_index - DMA半缓冲索引，只允许0或1
 * Output      : 对应DMA块首地址；索引无效时返回NULL
 * Description : 返回已经由DMA中断确认完成的半缓冲地址，不复制数据。
 *---------------------------------------------------------------------------*/
const uint16_t *drv_adc_completed_block(uint8_t block_index)
{
    return (block_index < 2U) ? g_adc_dma[block_index] : NULL;
}

/*---------------------------------------------------------------------------*
 * Name        : size_t drv_adc_block_words(void)
 * Input       : 无
 * Output      : 单个DMA完成块包含的16位采样字数
 * Description : 返回单个ADC完成块包含的16位采样字数。
 *---------------------------------------------------------------------------*/
size_t drv_adc_block_words(void)
{
    return DRV_ADC_BLOCK_WORDS;
}

/*---------------------------------------------------------------------------*
 * Name        : uint8_t drv_adc_dma_irq_ack(void)
 * Input       : 无
 * Output      : DRV_ADC_IRQ_*完成/错误位图
 * Description : 读取并清除DMA半传输、全传输和传输错误标志，返回供ISR发布的完成位图。
 *---------------------------------------------------------------------------*/
uint8_t drv_adc_dma_irq_ack(void)
{
    uint8_t completed = 0U;
    if (DDL_DMA_IsActiveFlag_HT1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_HT1(DMA);
        completed |= DRV_ADC_IRQ_BLOCK0;
    }
    if (DDL_DMA_IsActiveFlag_TC1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_TC1(DMA);
        completed |= DRV_ADC_IRQ_BLOCK1;
    }
    if (DDL_DMA_IsActiveFlag_TE1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_TE1(DMA);
        completed |= DRV_ADC_IRQ_ERROR;
    }
    return completed;
}
