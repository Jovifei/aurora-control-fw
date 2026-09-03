#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_adc.h"
#include "g32f031_ddl_bus.h"
#include "g32f031_ddl_dma.h"
#include "g32f031_ddl_gpio.h"
#include "g32f031_ddl_gtmr.h"
#include "g32f031_ddl_rcc.h"

static uint16_t g_adc_dma[2][DRV_ADC_BLOCK_WORDS];
static volatile uint16_t s_adc_raw[DRV_ADC_CHANNEL_COUNT];
static volatile uint16_t s_adc_average[DRV_ADC_CHANNEL_COUNT];
static volatile uint32_t s_sequence_count;

/*---------------------------------------------------------------------------*
 * Name        : static void publish_block_averages(uint8_t block_index)
 * Input       : block_index - 已完成的DMA半缓冲
 * Output      : 无
 * Description : 从完整块提取末次原始值和16次均值，供官方GetRaw/GetAverage读取。
 *---------------------------------------------------------------------------*/
static void publish_block_averages(uint8_t block_index)
{
    const uint16_t *block = g_adc_dma[block_index];
    uint32_t channel;
    uint32_t scan;
    uint32_t last = (uint32_t)(DRV_ADC_SCANS_PER_BLOCK - 1U) * DRV_ADC_CHANNEL_COUNT;

    for (channel = 0U; channel < DRV_ADC_CHANNEL_COUNT; ++channel)
    {
        uint32_t sum = 0U;
        s_adc_raw[channel] = block[last + channel];
        for (scan = 0U; scan < DRV_ADC_SCANS_PER_BLOCK; ++scan)
        {
            sum += block[(scan * DRV_ADC_CHANNEL_COUNT) + channel];
        }
        s_adc_average[channel] = (uint16_t)(sum / DRV_ADC_SCANS_PER_BLOCK);
    }
    s_sequence_count += DRV_ADC_SCANS_PER_BLOCK;
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_ADC_Init(void)
 * Input       : 无
 * Output      : 无
 * Description : 按官方Application配置模拟GPIO、GTMR TRGO 10kHz触发和64周期采样。
 *               产品仍保留六通道规则组与DMA双半缓冲，不改用五通道EOS轮询。
 *---------------------------------------------------------------------------*/
void BSP_ADC_Init(void)
{
    DDL_GPIO_InitTypeDef gpio = {0};
    DDL_ADC_InitTypeDef adc = {0};
    DDL_ADC_REG_InitTypeDef reg = {0};
    DDL_GTMR_InitTypeDef timer = {0};
    DDL_DMA_InitTypeDef dma = {0};

    DDL_RCC_Unlock();
    DDL_AHB_GRP1_EnableClock(DDL_AHB_GRP1_PERIPH_GPIOA | DDL_AHB_GRP1_PERIPH_GPIOB |
                             DDL_AHB_GRP1_PERIPH_DMA);
    DDL_APB_GRP1_EnableClock(DDL_APB_GRP1_PERIPH_ADC | DDL_APB_GRP1_PERIPH_GTMR);
    DDL_RCC_SetADCClkDiv(DDL_RCC_ADCCLK_DIVISION_4);
    DDL_RCC_Lock();

    /* GPIO：官方五路模拟输入，另保留PB1作为BST_U第六通道 */
    gpio.Mode = DDL_GPIO_MODE_ANALOG;
    gpio.Drive = DDL_GPIO_DRIVE_LOW;
    gpio.OutputType = DDL_GPIO_OUTPUT_PUSHPULL;
    gpio.InputEnable = DDL_GPIO_INPUT_ENABLE;
    gpio.Pull = DDL_GPIO_PULL_NO;
    gpio.Alternate = DDL_GPIO_AF_0;
    gpio.Pin = DDL_GPIO_PIN_8 | DDL_GPIO_PIN_9;
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOA, &gpio);
    DDL_GPIO_LockKey(GPIOA, DDL_GPIO_LOCK_ENABLE);
    gpio.Pin = DDL_GPIO_PIN_0 | DDL_GPIO_PIN_1 | DDL_GPIO_PIN_5 | DDL_GPIO_PIN_12;
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_DISABLE);
    DDL_GPIO_Init(GPIOB, &gpio);
    DDL_GPIO_LockKey(GPIOB, DDL_GPIO_LOCK_ENABLE);

    DDL_ADC_Enable(ADC);
    {
        uint32_t timeout = BOARD_ADC_READY_TIMEOUT_LOOPS;
        while ((DDL_ADC_IsActiveFlag_RDY(ADC) == 0U) && (timeout > 0U))
        {
            timeout--;
        }
    }

    adc.DataAlignment = DDL_ADC_ALIGNMENT_RIGHT;
    (void)DDL_ADC_Init(ADC, &adc);

    /* 官方：GTMR TRGO上升沿触发；产品保留六通道DMA */
    reg.TriggerSource = DDL_ADC_REG_TRIG_EXTSEL_GTMR_TRGO;    // GTMR TRGO上升沿触发
    reg.TriggerEdge = DDL_ADC_REG_TRIG_EXTEDGE_RISING;        // 上升沿触发
    reg.SequencerLength = DDL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS; // 6个通道
    reg.SequencerDiscont = DDL_ADC_REG_SEQ_DISCONT_DISABLE;   // 不连续模式
    reg.ContinuousMode = DDL_ADC_REG_CONV_SINGLE;             // 单次转换
    reg.DMATransfer = DDL_ADC_REG_DMA_TRANSFER_CIRCULAR_MODE; // 循环模式
    reg.Overrun = DDL_ADC_OVERMODE_KEEP;                      // 覆盖模式
    (void)DDL_ADC_REG_Init(ADC, &reg);

    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_1, DDL_ADC_CHANNEL_1);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_2, DDL_ADC_CHANNEL_2);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_3, DDL_ADC_CHANNEL_3);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_4, DDL_ADC_CHANNEL_4);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_5, DDL_ADC_CHANNEL_5);
    DDL_ADC_REG_SetSequencerRanks(ADC, DDL_ADC_REG_RANK_6, DDL_ADC_CHANNEL_6);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_1, DDL_ADC_SAMPLINGTIME_64_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_2, DDL_ADC_SAMPLINGTIME_64_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_3, DDL_ADC_SAMPLINGTIME_64_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_4, DDL_ADC_SAMPLINGTIME_64_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_5, DDL_ADC_SAMPLINGTIME_64_SCYCLES);
    DDL_ADC_SetChannelSamplingTime(ADC, DDL_ADC_CHANNEL_6, DDL_ADC_SAMPLINGTIME_64_SCYCLES);

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
    (void)DDL_DMA_Init(DMA, DDL_DMA_CHANNEL_1, &dma);
    DMA->IFCLR = 0xFFFFFFFFUL;
    DDL_DMA_EnableIT_HT(DMA, DDL_DMA_CHANNEL_1);
    DDL_DMA_EnableIT_TC(DMA, DDL_DMA_CHANNEL_1);
    DDL_DMA_EnableIT_TE(DMA, DDL_DMA_CHANNEL_1);
    NVIC_EnableIRQ(DMA_CH1_IRQn);

    DDL_ADC_ClearFlag_EOS(ADC);
    DDL_ADC_ClearFlag_OVR(ADC);
    DDL_ADC_EnableIT_OVR(ADC);
    NVIC_SetPriority(ADC_IRQn, 2U);
    NVIC_EnableIRQ(ADC_IRQn);

    /* GTMR：10kHz TRGO，不启动计数 */
    timer.Prescaler = (uint16_t)BOARD_ADC_GTMR_PRESCALER;
    timer.CounterMode = DDL_GTMR_COUNTERMODE_UP;
    timer.Autoreload = BOARD_ADC_GTMR_AUTORELOAD;
    timer.ClockDivision = DDL_GTMR_CLOCKDIVISION_DIV1;
    (void)DDL_GTMR_Init(GTMR, &timer);
    DDL_GTMR_SetTriggerOutput(GTMR, DDL_GTMR_TRGO_UPDATE);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_ADC_Start(void)
 * Input       : 无
 * Output      : 无
 * Description : 启动GTMR计数，开始以TRGO周期触发ADC规则组扫描。
 *---------------------------------------------------------------------------*/
void BSP_ADC_Start(void)
{
    DDL_GTMR_EnableCounter(GTMR);
}

/*---------------------------------------------------------------------------*
 * Name        : void BSP_ADC_IRQHandler(void)
 * Input       : 无
 * Output      : 无
 * Description : ADC OVR入口。数据路径由DMA搬运；此处只清除过载标志。
 *---------------------------------------------------------------------------*/
void BSP_ADC_IRQHandler(void)
{
    if (DDL_ADC_IsActiveFlag_OVR(ADC) != 0U)
    {
        DDL_ADC_ClearFlag_OVR(ADC);
    }
    if (DDL_ADC_IsActiveFlag_EOS(ADC) != 0U)
    {
        DDL_ADC_ClearFlag_EOS(ADC);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : uint16_t BSP_ADC_GetRaw(uint32_t index)
 * Input       : index - 通道索引
 * Output      : 最近一次完整扫描的原始值；越界返回0
 * Description : 主循环读取DMA发布块中的最后一次扫描值。
 *---------------------------------------------------------------------------*/
uint16_t BSP_ADC_GetRaw(uint32_t index)
{
    return (index < DRV_ADC_CHANNEL_COUNT) ? s_adc_raw[index] : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : uint16_t BSP_ADC_GetAverage(uint32_t index)
 * Input       : index - 通道索引
 * Output      : 最近一块16次扫描均值；越界返回0
 * Description : 与官方Application同名接口；产品测量仍以DMA块为准。
 *---------------------------------------------------------------------------*/
uint16_t BSP_ADC_GetAverage(uint32_t index)
{
    return (index < DRV_ADC_CHANNEL_COUNT) ? s_adc_average[index] : 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t BSP_ADC_GetSequenceCount(void)
 * Input       : 无
 * Output      : 累计完成的扫描序列次数
 * Description : 每完成一次规则组扫描递增，由DMA块完成次数推算。
 *---------------------------------------------------------------------------*/
uint32_t BSP_ADC_GetSequenceCount(void)
{
    return s_sequence_count;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_adc_init(void)
 * Input       : 无
 * Output      : true表示ADC规则组、DMA和GTMR初始化完成
 * Description : 调用官方BSP_ADC_Init。
 *---------------------------------------------------------------------------*/
bool drv_adc_init(void)
{
    uint32_t index;

    for (index = 0U; index < DRV_ADC_CHANNEL_COUNT; ++index)
    {
        s_adc_raw[index] = 0U;
        s_adc_average[index] = 0U;
    }
    s_sequence_count = 0U;
    BSP_ADC_Init();
    return DDL_ADC_IsActiveFlag_RDY(ADC) != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_adc_start(void)
 * Input       : 无
 * Output      : true表示DMA通道已使能且ADC规则组已武装
 * Description : 先使能DMA，再武装ADC规则组，最后启动官方GTMR触发源。
 *---------------------------------------------------------------------------*/
bool drv_adc_start(void)
{
    DDL_DMA_EnableChannel(DMA, DDL_DMA_CHANNEL_1);
    if (DDL_DMA_IsEnabledChannel(DMA, DDL_DMA_CHANNEL_1) == 0U)
    {
        return false;
    }

    DDL_ADC_StartConversion(ADC);
    BSP_ADC_Start();
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
 * Description : 读取并清除DMA半传输、全传输和传输错误标志。
 *---------------------------------------------------------------------------*/
uint8_t drv_adc_dma_irq_ack(void)
{
    uint8_t completed = 0U;
    if (DDL_DMA_IsActiveFlag_HT1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_HT1(DMA);
        publish_block_averages(0U);
        completed |= DRV_ADC_IRQ_BLOCK0;
    }
    if (DDL_DMA_IsActiveFlag_TC1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_TC1(DMA);
        publish_block_averages(1U);
        completed |= DRV_ADC_IRQ_BLOCK1;
    }
    if (DDL_DMA_IsActiveFlag_TE1(DMA) != 0U)
    {
        DDL_DMA_ClearFlag_TE1(DMA);
        completed |= DRV_ADC_IRQ_ERROR;
    }
    return completed;
}
