/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "openamp.h"
#include "sdmmc.h"
#include "spi.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "openamp_fs.h"
#include "emmc_fs.h"
#include "ads131m08.h"
#include "daq_engine.h"
#include "statemachine.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

#define TEST_FILE_NAME "test.dat"
#define TEST_FILE_SIZE_BYTES (1UL * 1024UL * 1024UL)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile DaqContext_t g_daq_ctx;
ADS131M08_HandleTypeDef ads;
GPIO_TypeDef *SYNC_RESET_GPIO_Port = ADS_SYNC_RESET_GPIO_Port;
uint16_t SYNC_RESET_Pin = ADS_SYNC_RESET_Pin;
static int32_t g_cm4_emmc_init_status = 0;
static int32_t g_cm4_emmc_mount_status = 0;
static int32_t g_cm4_emmc_create_status = 0;
static uint16_t g_cm4_adc_device_id = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
//static int32_t CM4_ReadThroughTestFile(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /*HW semaphore Clock enable*/
  __HAL_RCC_HSEM_CLK_ENABLE();
  /* Activate HSEM notification for Cortex-M4*/
  HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));
  /*
  Domain D2 goes to STOP mode (Cortex-M4 in deep-sleep) waiting for Cortex-M7 to
  perform system initialization (system clock config, external memory configuration.. )
  */
  HAL_PWREx_ClearPendingEvent();
  HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
  /* Clear HSEM flag */
  __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(HSEM_ID_0));

/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI4_Init();
  MX_SDMMC1_MMC_Init();
  /* USER CODE BEGIN 2 */
  ads.hspi = &hspi4;
  ads.cs_port = ADS_CS_GPIO_Port;
  ads.cs_pin = ADS_CS_Pin;

  adcMaster_Startup();
  HAL_Delay(10U);
  g_cm4_adc_device_id = getDeviceIdPattern();
  adcMaster_Shutdown();

  DAQ_ContextInit();
  DAQ_EngineInit();

  g_cm4_emmc_init_status = (int32_t)EmmcFs_Init();
  if (g_cm4_emmc_init_status != EMMC_FS_OK)
  {
	  Error_Handler();
  }

  g_cm4_emmc_mount_status = (int32_t)EmmcFs_MountOrFormat();

  g_cm4_emmc_create_status = (int32_t) EmmcFs_CreatePatternFile(TEST_FILE_NAME, TEST_FILE_SIZE_BYTES, NULL, NULL);

  if (OpenAmpFs_RemoteInit() != 0)
  {
    Error_Handler();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    OpenAmpFs_RemotePoll();
    DAQ_StateMachine_Run();
  }
  /* USER CODE END 3 */
}

/* USER CODE BEGIN 4 */
int32_t CM4_GetEmmcInitStatus(void)
{
  return g_cm4_emmc_init_status;
}

int32_t CM4_GetEmmcMountStatus(void)
{
  return g_cm4_emmc_mount_status;
}

uint16_t CM4_GetAdcDeviceId(void)
{
  return g_cm4_adc_device_id;
}

/*
 * Helper Functions
 */


/*
 * Callback
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if(GPIO_Pin == ADS_DRDY_Pin) {
    g_daq_ctx.events |= DAQ_EVT_ADC_READY;
	}
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
