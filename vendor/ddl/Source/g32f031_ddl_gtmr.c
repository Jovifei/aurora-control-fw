/**
  *
  * @file    g32f031_ddl_gtmr.c
  * @brief   GTMR DDL module driver.
  * @version V1.0.0
  * @date    2026-06-02
  *
  * @attention
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *
  * 1. Redistributions of source code must retain the above copyright notice,
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of the copyright holder nor the names of its contributors
  *    may be used to endorse or promote products derived from this software without
  *    specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
  * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
  * OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  * The original code has been modified by Geehy Semiconductor.
  *
  * Copyright (c) 2016 STMicroelectronics.
  * Copyright (C) 2026 Geehy Semiconductor.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  *
  */
#if defined(USE_FULL_DDL_DRIVER)

/* Includes ------------------------------------------------------------------*/
#include "g32f031_ddl_gtmr.h"
#include "g32f031_ddl_rcc.h"
#include "g32f031_ddl_bus.h"

#ifdef  USE_FULL_ASSERT
#include "g32_assert.h"
#else
#define ASSERT_PARAM(_PARAM_) ((void)0U)
#endif /* USE_FULL_ASSERT */

/** @addtogroup G32F031_DDL_Driver
  * @{
  */

#if defined (GTMR)

/** @addtogroup GTMR_DDL GTMR
  * @{
  */

/* Private macros ------------------------------------------------------------*/
/** @addtogroup GTMR_DDL_Private_Macros GTMR Private Macros
  * @{
  */
#define IS_GTMR_COUNTER_MODE_SELECT_INSTANCE(__VALUE__) ((__VALUE__) == GTMR)
#define IS_GTMR_CLOCK_DIVISION_INSTANCE(__VALUE__) ((__VALUE__) == GTMR)
#define IS_DDL_GTMR_COUNTERMODE(__VALUE__) (((__VALUE__) == DDL_GTMR_COUNTERMODE_UP) \
                                          || ((__VALUE__) == DDL_GTMR_COUNTERMODE_DOWN) \
                                          || ((__VALUE__) == DDL_GTMR_COUNTERMODE_CENTER_DOWN) \
                                          || ((__VALUE__) == DDL_GTMR_COUNTERMODE_CENTER_UP) \
                                          || ((__VALUE__) == DDL_GTMR_COUNTERMODE_CENTER_UP_DOWN))

#define IS_DDL_GTMR_CLOCKDIVISION(__VALUE__) (((__VALUE__) == DDL_GTMR_CLOCKDIVISION_DIV1) \
                                            || ((__VALUE__) == DDL_GTMR_CLOCKDIVISION_DIV2) \
                                            || ((__VALUE__) == DDL_GTMR_CLOCKDIVISION_DIV4))
/**
  * @}
  */

/* Exported functions --------------------------------------------------------*/
/** @addtogroup GTMR_DDL_Exported_Functions GTMR Exported Functions
  * @{
  */

/** @addtogroup GTMR_DDL_EF_Init
  * @{
  */

/**
  * @brief  Set TMRx registers to their reset values.
  * @param  TMRx Timer instance
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: TMRx registers are de-initialized
  *          - ERROR: invalid TMRx instance
  */
ErrorStatus DDL_GTMR_DeInit(GTMR_TypeDef *TMRx)
{
    ErrorStatus result = SUCCESS;

    ASSERT_PARAM(IS_GTMR_ALL_INSTANCE(TMRx));

    DDL_RCC_Unlock();
    if (TMRx == GTMR)
    {
        DDL_APB_GRP1_ForceReset(DDL_APB_GRP1_PERIPH_GTMR);
        DDL_APB_GRP1_ReleaseReset(DDL_APB_GRP1_PERIPH_GTMR);
    }
    else
    {
        result = ERROR;
    }
    DDL_RCC_Lock();

    return result;
}

/**
  * @brief  Set the fields of the time base unit configuration data structure
  *         to their default values.
  * @param  TMR_InitStruct pointer to a @ref DDL_GTMR_InitTypeDef structure
  * @retval None
  */
void DDL_GTMR_StructInit(DDL_GTMR_InitTypeDef *TMR_InitStruct)
{
    TMR_InitStruct->Prescaler = (uint16_t)0x0000U;
    TMR_InitStruct->CounterMode = DDL_GTMR_COUNTERMODE_UP;
    TMR_InitStruct->Autoreload = 0x0000FFFFU;
    TMR_InitStruct->ClockDivision = DDL_GTMR_CLOCKDIVISION_DIV1;
}

/**
  * @brief  Configure the TMRx time base unit.
  * @param  TMRx Timer Instance
  * @param  TMR_InitStruct pointer to a @ref DDL_GTMR_InitTypeDef structure
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: TMRx registers are initialized
  *          - ERROR: invalid TMRx instance
  */
ErrorStatus DDL_GTMR_Init(GTMR_TypeDef *TMRx, DDL_GTMR_InitTypeDef *TMR_InitStruct)
{
    uint32_t tmpcr1;

    ASSERT_PARAM(IS_GTMR_ALL_INSTANCE(TMRx));
    ASSERT_PARAM(IS_DDL_GTMR_COUNTERMODE(TMR_InitStruct->CounterMode));
    ASSERT_PARAM(IS_DDL_GTMR_CLOCKDIVISION(TMR_InitStruct->ClockDivision));

    tmpcr1 = DDL_GTMR_ReadReg(TMRx, CR1);

    if (IS_GTMR_COUNTER_MODE_SELECT_INSTANCE(TMRx))
    {
        MODIFY_REG(tmpcr1, (GTMR_CR1_CNTDIR | GTMR_CR1_CAMSEL), TMR_InitStruct->CounterMode);
    }

    if (IS_GTMR_CLOCK_DIVISION_INSTANCE(TMRx))
    {
        MODIFY_REG(tmpcr1, GTMR_CR1_CLKDIV, TMR_InitStruct->ClockDivision);
    }

    DDL_GTMR_WriteReg(TMRx, CR1, tmpcr1);
    DDL_GTMR_SetAutoReload(TMRx, TMR_InitStruct->Autoreload);
    DDL_GTMR_SetPrescaler(TMRx, TMR_InitStruct->Prescaler);
    DDL_GTMR_GenerateEvent_UPDATE(TMRx);

    return SUCCESS;
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

#endif /* GTMR */

/**
  * @}
  */

#endif /* USE_FULL_DDL_DRIVER */
