/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "hsem_lock.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef enum
{
  DAQ_STATE_IDLE = 0,
  DAQ_STATE_PREPARING,
  DAQ_STATE_ACQUIRING,
  DAQ_STATE_STOPPING,
  DAQ_STATE_FINALIZING,
  DAQ_STATE_ERROR
} DaqState_t;

typedef struct
{
  volatile DaqState_t state;
  volatile uint32_t events;
  volatile uint32_t last_error;
  volatile uint32_t samples_captured;
  volatile uint32_t dropped_buffers;
  volatile uint64_t bytes_queued;
  volatile uint64_t bytes_written;
  volatile uint32_t adc_ready_pending;
  volatile uint8_t is_adc_armed;
} DaqContext_t;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern volatile DaqContext_t g_daq_ctx;
int32_t CM4_GetEmmcInitStatus(void);
int32_t CM4_GetEmmcMountStatus(void);
uint16_t CM4_GetAdcDeviceId(void);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ADS_SPI_SCK_Pin GPIO_PIN_2
#define ADS_SPI_SCK_GPIO_Port GPIOE
#define ADS_DOUT_uC_DIN_Pin GPIO_PIN_5
#define ADS_DOUT_uC_DIN_GPIO_Port GPIOE
#define ADS_DIN_uC_DOUT_Pin GPIO_PIN_6
#define ADS_DIN_uC_DOUT_GPIO_Port GPIOE
#define ADS_DRDY_Pin GPIO_PIN_13
#define ADS_DRDY_GPIO_Port GPIOC
#define ADS_CS_Pin GPIO_PIN_6
#define ADS_CS_GPIO_Port GPIOF
#define ADS_SYNC_RESET_Pin GPIO_PIN_0
#define ADS_SYNC_RESET_GPIO_Port GPIOC
#define LDO_EN_Pin GPIO_PIN_11
#define LDO_EN_GPIO_Port GPIOF
#define DCDC_2_EN_Pin GPIO_PIN_14
#define DCDC_2_EN_GPIO_Port GPIOF
#define ADS_STM32_CLKOUT_Pin GPIO_PIN_8
#define ADS_STM32_CLKOUT_GPIO_Port GPIOA
#define eMMC_RSTn_Pin GPIO_PIN_0
#define eMMC_RSTn_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */


/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
