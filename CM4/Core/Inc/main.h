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

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);
void MX_SDMMC1_MMC_Init(void);

/* USER CODE BEGIN EFP */

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
#define LDO_EN_Pin GPIO_PIN_11
#define LDO_EN_GPIO_Port GPIOF
#define DCDC_2_EN_Pin GPIO_PIN_14
#define DCDC_2_EN_GPIO_Port GPIOF
#define ADS_STM32_CLKOUT_Pin GPIO_PIN_8
#define ADS_STM32_CLKOUT_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
