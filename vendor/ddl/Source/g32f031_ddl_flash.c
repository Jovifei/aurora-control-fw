/**
  *
  * @file    g32f031_ddl_flash.c
  * @brief   FLASH DDL module driver.
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
 *  Copyright (C) 2026 Geehy Semiconductor
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
#include "g32f031_ddl_flash.h"
#include "g32f031_ddl_rcc.h"

#ifdef  USE_FULL_ASSERT
#include "g32_assert.h"
#else
#define ASSERT_PARAM(_PARAM_) ((void)0U)
#endif /* USE_FULL_ASSERT */

/** @addtogroup G32F031_DDL_Driver
  * @{
  */

#if defined (FLASH)

/** @addtogroup FLASH_DDL FLASH
  * @{
  */

/* Private types -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private constants ---------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
/** @addtogroup FLASH_DDL_Exported_Functions FLASH Exported Functions
  * @{
  */

/** @addtogroup FLASH_DDL_EF_Init
  * @{
  */

/**
  * @brief  De-initialize FLASH registers (Registers restored to their default values).
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: FLASH registers are de-initialized
  *          - ERROR: FLASH registers are not de-initialized
  */
ErrorStatus DDL_FLASH_DeInit(void)
{
  ErrorStatus status = SUCCESS;

  FLASH->CR = 0x00000400U;
  FLASH->IER = 0x00000000U;
  FLASH->SR = 0x00000000U;
  FLASH->CR1 = 0x00000003U;

  return (status);
}

/**
  * @brief  Flash write.
  * @param  addr The address of data to download
  * @param  size The size of data to download
  * @param  buf  Data to download
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: FLASH registers are de-initialized
  *          - ERROR: FLASH registers are not de-initialized
  */
ErrorStatus DDL_FLASH_Write(uint32_t addr, uint32_t size, uint8_t *buf)
{
  ErrorStatus status = SUCCESS;
  uint32_t word;

  while(DDL_FLASH_IsActiveFlag_BUSY()){}

  DDL_FLASH_MKEY_Unlock();

  DDL_FLASH_SetOperationMode(DDL_FLASH_OPERATE_WRITE);

  /* word alignment */
  size &= ~3U;

  while (size)
  {
    word = ((uint32_t)buf[0]) |
           ((uint32_t)buf[1] << 8U) |
           ((uint32_t)buf[2] << 16U) |
           ((uint32_t)buf[3] << 24U);
    *((volatile uint32_t *)(uintptr_t)addr) = word;

    while (DDL_FLASH_IsActiveFlag_BUSY()){}

    if (DDL_FLASH_IsActiveFlag_PGERR())
    {
        DDL_FLASH_ClearFlag_PGERR();
        status = ERROR;
        return status;
    }

    while (!DDL_FLASH_IsActiveFlag_OPEND()) {}

    DDL_FLASH_ClearFlag_OPEND();

    addr += 4U;
    buf += 4U;
    size -= 4U;
  }

  return (status);
}

/**
  * @brief  Flash erase chip.
  * @param  FLASHx FLASH Instance
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: FLASH registers are de-initialized
  *          - ERROR: FLASH registers are not de-initialized
  */
ErrorStatus DDL_FLASH_EraseChip(void)
{
  ErrorStatus status = SUCCESS;

  while (DDL_FLASH_IsActiveFlag_BUSY()) {}

  DDL_FLASH_MKEY_Unlock();

  DDL_FLASH_SetOperationMode(DDL_FLASH_OPERATE_CHIPERASE);

  *((volatile uint32_t *)(uintptr_t)0x00000000UL) = 0x0000A5A5UL;

  while (DDL_FLASH_IsActiveFlag_BUSY()) {}
  while (!DDL_FLASH_IsActiveFlag_OPEND()) {}

  DDL_FLASH_ClearFlag_OPEND();


  return (status);
}

/**
  * @brief  Flash erase sector.
  * @param  FLASHx FLASH Instance
  * @param  addr   The address of data to erase
  * @retval An ErrorStatus enumeration value:
  *          - SUCCESS: FLASH registers are de-initialized
  *          - ERROR: FLASH registers are not de-initialized
  */
ErrorStatus DDL_FLASH_EraseSector(uint32_t addr)
{
  ErrorStatus status = SUCCESS;

  while (DDL_FLASH_IsActiveFlag_BUSY()) {}

  DDL_FLASH_MKEY_Unlock();

  DDL_FLASH_SetOperationMode(DDL_FLASH_OPERATE_SECTORERASE);

  *(uint32_t *)(addr) = 0xA5A5;

  while (DDL_FLASH_IsActiveFlag_BUSY()) {}
  while (!DDL_FLASH_IsActiveFlag_OPEND()) {}

  DDL_FLASH_ClearFlag_OPEND();

  return (status);
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

#endif /* defined (FLASH) */

/**
  * @}
  */

#endif /* USE_FULL_DDL_DRIVER */
