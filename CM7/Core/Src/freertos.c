/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lwip/ip_addr.h"
#include "queue.h"
#include <string.h>
#include "tcpclient.h"
#include "openamp_fs.h"
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

typedef struct
{
  uint32_t bytes_remaining;
} TestStreamContext_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TCP_FIXED_RESPONSE_LEN      100U
#define TCP_FILE_STREAM_HEADER_LEN  18U
#define TCP_FILE_STREAM_CHUNK_LEN   1400U
#define TCP_TEST_STREAM_TOTAL_SIZE  (1UL * 1024UL * 1024UL)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static QueueHandle_t gControlQueue;
static uint8_t g_test_stream_chunk[TCP_FILE_STREAM_CHUNK_LEN];
static TestStreamContext_t g_test_stream_ctx;
static TcpClientConfig_t tcpCfg;
static ip_addr_t tcpServerIp;
static ControlMessage_t msg;
extern struct netif gnetif;
/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId controllerTaskHandle;
osThreadId telemetryTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static uint32_t ReadU32Be(const uint8_t *data);
static uint64_t ReadU64Be(const uint8_t *data);
static uint16_t ReadU16Be(const uint8_t *data);
static void WriteU32Be(uint8_t *data, uint32_t value);
static void WriteU64Be(uint8_t *data, uint64_t value);
static void ProcessTcpData(const char *data, uint16_t length);
static void InitTestStreamChunk(void);
static int32_t TestStreamRead(void *context, uint8_t *buffer, uint16_t max_len, uint16_t *out_len);
static void TestStreamDone(void *context);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void ControllerTask(void const * argument);
void TelemetryTask(void const * argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  InitTestStreamChunk();
  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  gControlQueue = xQueueCreate(8U, sizeof(ControlMessage_t));
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityHigh, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of controllerTask */
  osThreadDef(controllerTask, ControllerTask, osPriorityNormal, 0, 2048);
  controllerTaskHandle = osThreadCreate(osThread(controllerTask), NULL);

  /* definition and creation of telemetryTask */
  osThreadDef(telemetryTask, TelemetryTask, osPriorityLow, 0, 128);
  telemetryTaskHandle = osThreadCreate(osThread(telemetryTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* USER CODE END RTOS_THREADS */

}

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
  /* USER CODE BEGIN StartDefaultTask */
  IP4_ADDR(&tcpServerIp, 192, 168, 0, 20);
  TcpClient_BuildConfig(&tcpCfg, &tcpServerIp, 10U, &gnetif, ProcessTcpData);
  (void)TcpClient_Init(&tcpCfg);

  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
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

        case 9U:
        {
          uint8_t header[TCP_FILE_STREAM_HEADER_LEN];
          int32_t stream_status;

          memset(header, 0, sizeof(header));
          header[0] = msg.command;
          header[1] = 0U;
          WriteU32Be(&header[2], msg.server_id);
          WriteU64Be(&header[6], msg.epoch_time);
          WriteU32Be(&header[14], TCP_TEST_STREAM_TOTAL_SIZE);

          g_test_stream_ctx.bytes_remaining = TCP_TEST_STREAM_TOTAL_SIZE;
          stream_status = TcpClient_StartStream(header,
                                                sizeof(header),
                                                TCP_TEST_STREAM_TOTAL_SIZE,
                                                TestStreamRead,
                                                TestStreamDone,
                                                &g_test_stream_ctx);
          if (stream_status != 0)
          {
            g_test_stream_ctx.bytes_remaining = 0U;
          }
          break;
        }

        case 1U:
        case 4U:
        case 5U:
        case 6U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[19], 0xFFFFFFFFU);
          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 2U:
        case 3U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[19], 0xFFFFFFF6U);
          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 99U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          uint32_t request_value;
          uint32_t reply_value = 0U;
          int32_t ping_status;

          request_value = (uint32_t)msg.epoch_time ^ msg.server_id ^ 0x00000063U;
          ping_status = OpenAmpFs_Ping(request_value, &reply_value);

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = (ping_status == 0) ? 0U : 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[15], reply_value);
          WriteU32Be(&tx[19], (uint32_t)ping_status);
          WriteU32Be(&tx[23], OpenAmpFs_GetServiceCreated());
          WriteU32Be(&tx[27], OpenAmpFs_GetRxCount());
          WriteU32Be(&tx[31], (uint32_t)OpenAmpFs_GetInitStatus());
          WriteU32Be(&tx[35], (uint32_t)OpenAmpFs_GetRemoteInitStatus());
          WriteU32Be(&tx[39], (uint32_t)OpenAmpFs_GetRemoteMountStatus());
          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        default:
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
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END TelemetryTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE BEGIN Helpers */
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

static void InitTestStreamChunk(void)
{
  uint32_t index;

  for (index = 0U; index < TCP_FILE_STREAM_CHUNK_LEN; index++)
  {
    g_test_stream_chunk[index] = (uint8_t)(((index * 37U) + 11U) & 0xFFU);
  }
}

static int32_t TestStreamRead(void *context, uint8_t *buffer, uint16_t max_len, uint16_t *out_len)
{
  TestStreamContext_t *stream_ctx = (TestStreamContext_t *)context;
  uint16_t copy_len;

  if ((stream_ctx == NULL) || (buffer == NULL) || (out_len == NULL) || (stream_ctx->bytes_remaining == 0U))
  {
    return -1;
  }

  copy_len = (stream_ctx->bytes_remaining > max_len) ? max_len : (uint16_t)stream_ctx->bytes_remaining;
  memcpy(buffer, g_test_stream_chunk, copy_len);
  stream_ctx->bytes_remaining -= copy_len;
  *out_len = copy_len;
  return 0;
}

static void TestStreamDone(void *context)
{
  TestStreamContext_t *stream_ctx = (TestStreamContext_t *)context;

  if (stream_ctx != NULL)
  {
    stream_ctx->bytes_remaining = 0U;
  }
}
/* USER CODE END Helpers */

/* USER CODE END Application */
