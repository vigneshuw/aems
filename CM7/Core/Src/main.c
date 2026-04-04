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
#include "cmsis_os.h"
#include "fatfs.h"
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bluenrg2_intf.h"
#include "led.h"
#include "ff_gen_drv.h"
#include "emmc_fs.h"
#include "hsem_ids.h"
#include "ipc.h"
#include "ipc_shared.h"
#include "mmc_diskio.h"
#include "shared_memory.h"
#include "queue.h"
#include <stdio.h>
#include <string.h>
#include "tcpclient.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  uint8_t command;
  uint32_t server_id;
  uint64_t epoch_time;
  uint16_t payload_len;
  uint8_t payload[85];
} ControlMessage_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#ifndef HSEM_ID_0
#define HSEM_ID_0 (0U) /* HW semaphore 0*/
#endif

#define TCP_FIXED_RESPONSE_LEN         100U
#define TCP_CONFIG_READ_HEADER_LEN     24U
#define TCP_CONFIG_READ_CHUNK_LEN      1024U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

MMC_HandleTypeDef hmmc1;

TIM_HandleTypeDef htim1;

osThreadId defaultTaskHandle;
osThreadId controllerTaskHandle;
osThreadId telemetryTaskHandle;
osThreadId fileTaskHandle;
/* USER CODE BEGIN PV */
static QueueHandle_t gControlQueue;
static QueueHandle_t gFileQueue;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_SDMMC1_MMC_Init(void);
void StartDefaultTask(void const * argument);
void ControllerTask(void const * argument);
void TelemetryTask(void const * argument);
void FileTask(void const * argument);

/* USER CODE BEGIN PFP */
static uint32_t ReadU32Be(const uint8_t *data);
static uint64_t ReadU64Be(const uint8_t *data);
static uint16_t ReadU16Be(const uint8_t *data);
static void WriteU16Be(uint8_t *data, uint16_t value);
static void WriteU32Be(uint8_t *data, uint32_t value);
static void WriteU64Be(uint8_t *data, uint64_t value);
static void ProcessTcpData(const char *data, uint16_t length);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
LED rgbLed;
uint16_t phyid1, phyid2, bmsr;
static uint32_t ipc_seq = 1U;

extern struct netif gnetif;


FATFS MMCFatFs;  /* File system object for SD card logical drive */
FIL daqFile;     /* File object */
char MMCPath[4]; /* SD card logical drive path */

uint8_t workBuffer[_MAX_SS];
ALIGN_32BYTES(uint8_t rtext[96]);

uint8_t wtext[] = "This is FatFs running on CM7 core"; /* File write buffer */

/*
 * BLE
 */

static uint32_t ReadU32Be(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         (uint32_t)data[3];
}

static uint64_t ReadU64Be(const uint8_t *data)
{
  return ((uint64_t)data[0] << 56) |
         ((uint64_t)data[1] << 48) |
         ((uint64_t)data[2] << 40) |
         ((uint64_t)data[3] << 32) |
         ((uint64_t)data[4] << 24) |
         ((uint64_t)data[5] << 16) |
         ((uint64_t)data[6] << 8) |
         (uint64_t)data[7];
}

static uint16_t ReadU16Be(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static void WriteU32Be(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static void WriteU16Be(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

static void WriteU64Be(uint8_t *data, uint64_t value)
{
  data[0] = (uint8_t)(value >> 56);
  data[1] = (uint8_t)(value >> 48);
  data[2] = (uint8_t)(value >> 40);
  data[3] = (uint8_t)(value >> 32);
  data[4] = (uint8_t)(value >> 24);
  data[5] = (uint8_t)(value >> 16);
  data[6] = (uint8_t)(value >> 8);
  data[7] = (uint8_t)value;
}

static void ProcessTcpData(const char *data, uint16_t length)
{
  ControlMessage_t msg;
  uint16_t payload_len;
  uint16_t available_len;

  if ((data == NULL) || (length < 15U) || (gControlQueue == NULL))
  {
    return;
  }

  memset(&msg, 0, sizeof(msg));
  msg.command = (uint8_t)data[0];
  msg.server_id = ReadU32Be((const uint8_t *)&data[1]);
  msg.epoch_time = ReadU64Be((const uint8_t *)&data[5]);
  msg.payload_len = ReadU16Be((const uint8_t *)&data[13]);

  available_len = (uint16_t)(length - 15U);
  payload_len = msg.payload_len;
  if (payload_len > available_len)
  {
    payload_len = available_len;
  }

  if (payload_len > sizeof(msg.payload))
  {
    payload_len = sizeof(msg.payload);
  }

  msg.payload_len = payload_len;
  if (payload_len > 0U)
  {
    memcpy(msg.payload, &data[15], payload_len);
  }

  (void)xQueueSend(gControlQueue, &msg, 0U);
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */
/* USER CODE BEGIN Boot_Mode_Sequence_0 */
  int32_t timeout;
/* USER CODE END Boot_Mode_Sequence_0 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

/* USER CODE BEGIN Boot_Mode_Sequence_1 */
  /* Wait until CPU2 boots and enters in stop mode or timeout*/
  timeout = 0xFFFF;
  while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET) && (timeout-- > 0));
  if ( timeout < 0 )
  {
  Error_Handler();
  }
/* USER CODE END Boot_Mode_Sequence_1 */
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();
/* USER CODE BEGIN Boot_Mode_Sequence_2 */
/* When system initialization is finished, Cortex-M7 will release Cortex-M4 by means of
HSEM notification */
/*HW semaphore Clock enable*/
__HAL_RCC_HSEM_CLK_ENABLE();
/*Take HSEM */
HAL_HSEM_FastTake(HSEM_ID_0);
/*Release HSEM in order to notify the CPU2(CM4)*/
HAL_HSEM_Release(HSEM_ID_0,0);
/* wait until CPU2 wakes up from stop mode */
timeout = 0xFFFF;
while((__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET) && (timeout-- > 0));
if ( timeout < 0 )
{
Error_Handler();
}
/* USER CODE END Boot_Mode_Sequence_2 */

  /* USER CODE BEGIN SysInit */


  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_SDMMC1_MMC_Init();
  /* USER CODE BEGIN 2 */

  /*
   * Initialize
   */

  // LEDs
  HAL_TIM_Base_Start_IT(&htim1);
  LED_Init(&rgbLed, &htim1, TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3);
  LED_ON(&rgbLed);
  LED_SetBrightness(&rgbLed, 0, 40, 0);

  // Initialize BLE
  BlueNRG_Init();
  BlueNRG_StartAdvertising();

//  // Do a reset here
//  HAL_Delay(100);
//  HAL_GPIO_WritePin(ETH_NRST_GPIO_Port, ETH_NRST_Pin, GPIO_PIN_RESET);
//  HAL_Delay(100);
//  HAL_GPIO_WritePin(ETH_NRST_GPIO_Port, ETH_NRST_Pin, GPIO_PIN_SET);
//  HAL_Delay(1000);

  if (EmmcFs_Init() != EMMC_FS_OK)
  {
    LED_SetBrightness(&rgbLed, 40, 0, 0);
    Error_Handler();
  }

  if (EmmcFs_MountOrFormat() != EMMC_FS_OK)
  {
    LED_SetBrightness(&rgbLed, 40, 0, 0);
    Error_Handler();
  }

  IPC_InitSharedRegion();

  /* USER CODE END 2 */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  gControlQueue = xQueueCreate(8U, sizeof(ControlMessage_t));
  gFileQueue = xQueueCreate(4U, sizeof(ControlMessage_t));
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityHigh, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of controllerTask */
  osThreadDef(controllerTask, ControllerTask, osPriorityNormal, 0, 256);
  controllerTaskHandle = osThreadCreate(osThread(controllerTask), NULL);

  /* definition and creation of telemetryTask */
  osThreadDef(telemetryTask, TelemetryTask, osPriorityLow, 0, 128);
  telemetryTaskHandle = osThreadCreate(osThread(telemetryTask), NULL);

  /* definition and creation of fileTask */
  osThreadDef(fileTask, FileTask, osPriorityHigh, 0, 512);
  fileTaskHandle = osThreadCreate(osThread(fileTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 64;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_8);
}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_MMC_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */

  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hmmc1.Instance = SDMMC1;
  hmmc1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hmmc1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hmmc1.Init.BusWide = SDMMC_BUS_WIDE_8B;
  hmmc1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hmmc1.Init.ClockDiv = 3;
  if (HAL_MMC_Init(&hmmc1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC1_Init 2 */

  /* USER CODE END SDMMC1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 500;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI5_CS_GPIO_Port, SPI5_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ETH_NRST_GPIO_Port, ETH_NRST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BLE_RST_GPIO_Port, BLE_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : BLE_INT_Pin */
  GPIO_InitStruct.Pin = BLE_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BLE_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI5_CS_Pin */
  GPIO_InitStruct.Pin = SPI5_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(SPI5_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ETH_NRST_Pin */
  GPIO_InitStruct.Pin = ETH_NRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(ETH_NRST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BLE_RST_Pin */
  GPIO_InitStruct.Pin = BLE_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BLE_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PD0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PD7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_SDIO2;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : PG9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(BLE_INT_EXTI_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(BLE_INT_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_Delay(5000);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN 5 */
  TcpClientConfig_t tcpCfg;
  ip_addr_t tcpServerIp;
  //  IpcResponseBlock_t ipc_rsp;
  //  SharedStatusBlock_t ipc_status;

  IP4_ADDR(&tcpServerIp, 192, 168, 0, 20);
  TcpClient_BuildConfig(&tcpCfg, &tcpServerIp, 10U, &gnetif, ProcessTcpData);
  (void)TcpClient_Init(&tcpCfg);

  /* Infinite loop */
  for(;;)
  {
	 osDelay(1);

//    IPC_ReadStatus(&ipc_status);
//
//    if (IPC_PostSimpleCommand(ipc_seq, IPC_CMD_GET_STATUS) != 0U)
//    {
//      if (IPC_WaitForResponse(ipc_seq, &ipc_rsp, 100U) != 0U)
//      {
//        if (ipc_rsp.result == IPC_CMD_RES_OK)
//        {
//          ipc_seq++;
//        }
//      }
//    }




  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_ControllerTask */
/**
* @brief Function implementing the controllerTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_ControllerTask */
void ControllerTask(void const * argument)
{
  /* USER CODE BEGIN ControllerTask */
  ControlMessage_t msg;

  /* Infinite loop */
  for(;;)
  {
    if ((gControlQueue != NULL) &&
        (xQueueReceive(gControlQueue, &msg, portMAX_DELAY) == pdPASS))
    {
      switch (msg.command)
      {
        case 0U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];

        memset(tx, 0, sizeof(tx));
        tx[0] = msg.command;
        WriteU32Be(&tx[1], msg.server_id);
        WriteU64Be(&tx[5], msg.epoch_time);
        tx[13] = 0U;
        tx[14] = TcpClient_IsConnected();

        (void)TcpClient_SendBuffer(tx, sizeof(tx));
        break;
      }

      case 2U:
        if (gFileQueue != NULL)
        {
          (void)xQueueSend(gFileQueue, &msg, 0U);
        }
        break;

        case 1U:
          if (gFileQueue != NULL)
          {
            (void)xQueueSend(gFileQueue, &msg, 0U);
          }
          break;

        case 3U:
          if (gFileQueue != NULL)
          {
            (void)xQueueSend(gFileQueue, &msg, 0U);
          }
          break;

        case 4U:
          if (gFileQueue != NULL)
          {
            (void)xQueueSend(gFileQueue, &msg, 0U);
          }
          break;

        case 5U:
          if (gFileQueue != NULL)
          {
            (void)xQueueSend(gFileQueue, &msg, 0U);
          }
          break;

        default:
          /* TODO: Dispatch command to board features. */
          break;
      }
    }
  }
  /* USER CODE END ControllerTask */
}

/* USER CODE BEGIN Header_TelemetryTask */
/**
* @brief Function implementing the telemetryTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_TelemetryTask */
void TelemetryTask(void const * argument)
{
  /* USER CODE BEGIN TelemetryTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END TelemetryTask */
}

/* USER CODE BEGIN Header_FileTask */
/**
* @brief Function implementing the fileTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_FileTask */
void FileTask(void const * argument)
{
  /* USER CODE BEGIN FileTask */
  ControlMessage_t msg;

  /* Infinite loop */
  for(;;)
  {
    if ((gFileQueue != NULL) &&
        (xQueueReceive(gFileQueue, &msg, portMAX_DELAY) == pdPASS))
    {
      switch (msg.command)
      {
        case 1U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
        EmmcFsStatus_t fs_status;

        memset(tx, 0, sizeof(tx));
        tx[0] = msg.command;
        WriteU32Be(&tx[1], msg.server_id);
        WriteU64Be(&tx[5], msg.epoch_time);
        tx[14] = TcpClient_IsConnected();

        fs_status = EmmcFs_WriteConfigMain(msg.server_id,
                                           msg.epoch_time,
                                           msg.payload,
                                           msg.payload_len);
        tx[13] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);

        (void)TcpClient_SendBuffer(tx, sizeof(tx));
        break;
      }

        case 2U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          EmmcFsDatSummary_t summary;
          EmmcFsStatus_t fs_status;

        memset(tx, 0, sizeof(tx));
        tx[0] = msg.command;
        WriteU32Be(&tx[1], msg.server_id);
        WriteU64Be(&tx[5], msg.epoch_time);
        tx[14] = TcpClient_IsConnected();

        fs_status = EmmcFs_CountDatFiles(&summary);
        tx[13] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);

        if (fs_status == EMMC_FS_OK)
        {
          WriteU32Be(&tx[15], summary.dat_file_count);
        }

          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 3U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          uint32_t total_file_count = 0U;
          EmmcFsStatus_t fs_status;

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[14] = TcpClient_IsConnected();

          fs_status = EmmcFs_CountAllFiles(&total_file_count);
          tx[13] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);

          if (fs_status == EMMC_FS_OK)
          {
            WriteU32Be(&tx[15], total_file_count);
          }

          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 4U:
        {
          static uint8_t tx[TCP_CONFIG_READ_HEADER_LEN + TCP_CONFIG_READ_CHUNK_LEN];
          uint16_t tx_len;
          uint32_t offset = 0U;
          uint32_t total_size = 0U;
          uint16_t chunk_len = 0U;
          EmmcFsStatus_t fs_status;

          do
          {
            memset(tx, 0, sizeof(tx));
            tx[0] = msg.command;
            tx[1] = 0U;
            WriteU32Be(&tx[2], msg.server_id);
            WriteU64Be(&tx[6], msg.epoch_time);

            fs_status = EmmcFs_ReadConfigMainChunk(offset,
                                                   &tx[TCP_CONFIG_READ_HEADER_LEN],
                                                   TCP_CONFIG_READ_CHUNK_LEN,
                                                   &chunk_len,
                                                   &total_size);
            tx[1] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);
            WriteU32Be(&tx[14], total_size);
            WriteU32Be(&tx[18], offset);
            WriteU16Be(&tx[22], chunk_len);

            tx_len = (uint16_t)(TCP_CONFIG_READ_HEADER_LEN + chunk_len);
            (void)TcpClient_SendBuffer(tx, tx_len);

            if ((fs_status != EMMC_FS_OK) || (chunk_len == 0U))
            {
              break;
            }

            offset += chunk_len;
            osDelay(2);
          } while (offset < total_size);

          break;
        }

        case 5U:
        {
          static uint8_t tx[TCP_CONFIG_READ_HEADER_LEN + TCP_CONFIG_READ_CHUNK_LEN];
          char filename[86];
          uint16_t tx_len;
          uint32_t offset = 0U;
          uint32_t total_size = 0U;
          uint16_t chunk_len = 0U;
          EmmcFsStatus_t fs_status;

          memset(filename, 0, sizeof(filename));
          if (msg.payload_len == 0U)
          {
            fs_status = EMMC_FS_ERR_PARAM;
          }
          else
          {
            memcpy(filename, msg.payload, msg.payload_len);
            filename[msg.payload_len] = '\0';
            fs_status = EMMC_FS_OK;
          }

          do
          {
            memset(tx, 0, sizeof(tx));
            tx[0] = msg.command;
            tx[1] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);
            WriteU32Be(&tx[2], msg.server_id);
            WriteU64Be(&tx[6], msg.epoch_time);

            if (fs_status == EMMC_FS_OK)
            {
              fs_status = EmmcFs_ReadFileChunk(filename,
                                               offset,
                                               &tx[TCP_CONFIG_READ_HEADER_LEN],
                                               TCP_CONFIG_READ_CHUNK_LEN,
                                               &chunk_len,
                                               &total_size);
              tx[1] = (uint8_t)((fs_status == EMMC_FS_OK) ? 0U : 1U);
            }
            else
            {
              chunk_len = 0U;
              total_size = 0U;
            }

            WriteU32Be(&tx[14], total_size);
            WriteU32Be(&tx[18], offset);
            WriteU16Be(&tx[22], chunk_len);

            tx_len = (uint16_t)(TCP_CONFIG_READ_HEADER_LEN + chunk_len);
            (void)TcpClient_SendBuffer(tx, tx_len);

            if ((fs_status != EMMC_FS_OK) || (chunk_len == 0U))
            {
              break;
            }

            offset += chunk_len;
            osDelay(2);
          } while (offset < total_size);

          break;
        }

        default:
          break;
        }
    }
  }
  /* USER CODE END FileTask */
}

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30020000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x30040000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512B;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4KB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
