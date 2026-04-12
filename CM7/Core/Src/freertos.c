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

typedef struct
{
  char filename[65];
  uint32_t total_size;
  uint32_t offset;
  int32_t last_status;
} OpenAmpFileStreamContext_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TCP_FIXED_RESPONSE_LEN      100U
#define TCP_FILE_STREAM_HEADER_LEN  18U
#define TCP_FILE_STREAM_CHUNK_LEN   1400U
#define OPENAMP_FILE_CHUNK_LEN      1024U
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
static OpenAmpFileStreamContext_t g_openamp_file_stream_ctx;
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
static int32_t OpenAmpFileStreamRead(void *context, uint8_t *buffer, uint16_t max_len, uint16_t *out_len);
static void OpenAmpFileStreamDone(void *context);
static void CopyFilenameFromPayload(const uint8_t *payload,
                                    uint16_t payload_len,
                                    uint16_t payload_offset,
                                    char *filename,
                                    uint16_t filename_size);
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

        case 8U:
        {
          uint8_t header[TCP_FILE_STREAM_HEADER_LEN];
          uint32_t file_size = 0U;
          uint32_t start_offset = 0U;
          uint32_t stream_size = 0U;
          int32_t fs_status;
          int32_t stream_status;

          memset(&g_openamp_file_stream_ctx, 0, sizeof(g_openamp_file_stream_ctx));
          if (msg.payload_len >= 4U)
          {
            start_offset = ReadU32Be(msg.payload);
            CopyFilenameFromPayload(msg.payload,
                                    msg.payload_len,
                                    4U,
                                    g_openamp_file_stream_ctx.filename,
                                    sizeof(g_openamp_file_stream_ctx.filename));
          }
          else
          {
            CopyFilenameFromPayload(msg.payload,
                                    msg.payload_len,
                                    0U,
                                    g_openamp_file_stream_ctx.filename,
                                    sizeof(g_openamp_file_stream_ctx.filename));
          }

          fs_status = OpenAmpFs_OpenFileStream(g_openamp_file_stream_ctx.filename,
                                               start_offset,
                                               &file_size);
          if ((fs_status == 0) && (start_offset < file_size))
          {
            stream_size = file_size - start_offset;
            g_openamp_file_stream_ctx.total_size = file_size;
            g_openamp_file_stream_ctx.offset = start_offset;
          }
          else
          {
            fs_status = (fs_status == 0) ? -1 : fs_status;
            (void)OpenAmpFs_CloseFileStream();
          }

          memset(header, 0, sizeof(header));
          header[0] = msg.command;
          header[1] = (fs_status == 0) ? 0U : 1U;
          WriteU32Be(&header[2], msg.server_id);
          WriteU64Be(&header[6], msg.epoch_time);
          WriteU32Be(&header[14], stream_size);

          if (fs_status == 0)
          {
            stream_status = TcpClient_StartStream(header,
                                                  sizeof(header),
                                                  stream_size,
                                                  OpenAmpFileStreamRead,
                                                  OpenAmpFileStreamDone,
                                                  &g_openamp_file_stream_ctx);
            if (stream_status != 0)
            {
              g_openamp_file_stream_ctx.last_status = stream_status;
              (void)OpenAmpFs_CloseFileStream();
            }
          }
          else
          {
            (void)TcpClient_SendBuffer(header, sizeof(header));
          }
          break;
        }

        case 1U:
        case 4U:
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

        case 5U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          char filename[65];
          uint16_t filename_len;
          uint32_t file_size = 0U;
          int32_t fs_status;

          memset(filename, 0, sizeof(filename));
          filename_len = msg.payload_len;
          if (filename_len >= sizeof(filename))
          {
            filename_len = (uint16_t)(sizeof(filename) - 1U);
          }

          if (filename_len > 0U)
          {
            memcpy(filename, msg.payload, filename_len);
          }

          fs_status = OpenAmpFs_GetFileSize(filename, &file_size);

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = (fs_status == 0) ? 0U : 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[15], file_size);
          WriteU32Be(&tx[19], (uint32_t)fs_status);
          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 7U:
        {
          uint8_t tx[24U + 1024U];
          char filename[65];
          uint16_t filename_len;
          uint32_t read_offset = 0U;
          uint16_t bytes_read = 0U;
          uint32_t total_size = 0U;
          int32_t fs_status;

          memset(filename, 0, sizeof(filename));
          if (msg.payload_len >= 4U)
          {
            read_offset = ReadU32Be(msg.payload);
            filename_len = (uint16_t)(msg.payload_len - 4U);
            if (filename_len >= sizeof(filename))
            {
              filename_len = (uint16_t)(sizeof(filename) - 1U);
            }

            if (filename_len > 0U)
            {
              memcpy(filename, &msg.payload[4], filename_len);
            }
          }
          else
          {
            filename_len = msg.payload_len;
            if (filename_len >= sizeof(filename))
            {
              filename_len = (uint16_t)(sizeof(filename) - 1U);
            }

            if (filename_len > 0U)
            {
              memcpy(filename, msg.payload, filename_len);
            }
          }

          memset(tx, 0, sizeof(tx));
          fs_status = OpenAmpFs_ReadFileChunk(filename,
                                              read_offset,
                                              &tx[24],
                                              1024U,
                                              &bytes_read,
                                              &total_size);

          tx[0] = msg.command;
          tx[1] = (fs_status == 0) ? 0U : 1U;
          WriteU32Be(&tx[2], msg.server_id);
          WriteU64Be(&tx[6], msg.epoch_time);
          WriteU32Be(&tx[14], total_size);
          WriteU32Be(&tx[18], read_offset);
          tx[22] = (uint8_t)(bytes_read >> 8);
          tx[23] = (uint8_t)bytes_read;
          (void)TcpClient_SendBuffer(tx, (uint16_t)(24U + bytes_read));
          break;
        }

        case 2U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          uint32_t dat_count = 0U;
          int32_t fs_status;

          fs_status = OpenAmpFs_CountDatFiles(&dat_count);

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = (fs_status == 0) ? 0U : 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[15], dat_count);
          WriteU32Be(&tx[19], (uint32_t)fs_status);
          (void)TcpClient_SendBuffer(tx, sizeof(tx));
          break;
        }

        case 3U:
        {
          uint8_t tx[TCP_FIXED_RESPONSE_LEN];
          uint32_t total_count = 0U;
          int32_t fs_status;

          fs_status = OpenAmpFs_CountAllFiles(&total_count);

          memset(tx, 0, sizeof(tx));
          tx[0] = msg.command;
          WriteU32Be(&tx[1], msg.server_id);
          WriteU64Be(&tx[5], msg.epoch_time);
          tx[13] = (fs_status == 0) ? 0U : 1U;
          tx[14] = TcpClient_IsConnected();
          WriteU32Be(&tx[15], total_count);
          WriteU32Be(&tx[19], (uint32_t)fs_status);
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

static void CopyFilenameFromPayload(const uint8_t *payload,
                                    uint16_t payload_len,
                                    uint16_t payload_offset,
                                    char *filename,
                                    uint16_t filename_size)
{
  uint16_t filename_len;

  if ((payload == NULL) || (filename == NULL) || (filename_size == 0U))
  {
    return;
  }

  memset(filename, 0, filename_size);
  if (payload_offset >= payload_len)
  {
    return;
  }

  filename_len = (uint16_t)(payload_len - payload_offset);
  if (filename_len >= filename_size)
  {
    filename_len = (uint16_t)(filename_size - 1U);
  }

  if (filename_len > 0U)
  {
    memcpy(filename, &payload[payload_offset], filename_len);
  }
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

static int32_t OpenAmpFileStreamRead(void *context, uint8_t *buffer, uint16_t max_len, uint16_t *out_len)
{
  OpenAmpFileStreamContext_t *stream_ctx = (OpenAmpFileStreamContext_t *)context;
  uint16_t request_len;
  uint16_t bytes_read = 0U;
  uint32_t total_size = 0U;
  uint32_t read_offset = 0U;
  uint32_t remaining;
  int32_t status;

  if ((stream_ctx == NULL) || (buffer == NULL) || (out_len == NULL))
  {
    return -1;
  }

  *out_len = 0U;
  if (stream_ctx->offset >= stream_ctx->total_size)
  {
    return -1;
  }

  remaining = stream_ctx->total_size - stream_ctx->offset;
  request_len = (remaining > max_len) ? max_len : (uint16_t)remaining;
  if (request_len > OPENAMP_FILE_CHUNK_LEN)
  {
    request_len = OPENAMP_FILE_CHUNK_LEN;
  }

  status = OpenAmpFs_ReadFileStream(buffer,
                                    request_len,
                                    &bytes_read,
                                    &read_offset,
                                    &total_size);
  stream_ctx->last_status = status;
  if ((status != 0) || (bytes_read == 0U))
  {
    return -1;
  }

  stream_ctx->offset = read_offset + bytes_read;
  *out_len = bytes_read;
  return 0;
}

static void OpenAmpFileStreamDone(void *context)
{
  OpenAmpFileStreamContext_t *stream_ctx = (OpenAmpFileStreamContext_t *)context;

  if (stream_ctx != NULL)
  {
    stream_ctx->offset = stream_ctx->total_size;
  }

  (void)OpenAmpFs_CloseFileStream();
}
/* USER CODE END Helpers */

/* USER CODE END Application */
