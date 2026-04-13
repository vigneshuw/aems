#include "openamp_fs.h"

#include <string.h>

#include "cmsis_os.h"
#include "openamp.h"
#include "file_shmem.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_TIMEOUT_MS 3000U

#define OPENAMP_OP_PING        99U
#define OPENAMP_OP_COUNT_DAT   2U
#define OPENAMP_OP_COUNT_ALL   3U
#define OPENAMP_OP_FILE_SIZE   5U
#define OPENAMP_OP_READ_CHUNK  7U
#define OPENAMP_OP_STREAM_OPEN 80U
#define OPENAMP_OP_STREAM_READ 81U
#define OPENAMP_OP_STREAM_CLOSE 82U
#define OPENAMP_OP_STREAM_READ_SHMEM 83U
#define OPENAMP_OP_SHMEM_PROBE 84U
#define OPENAMP_FILENAME_LEN   64U
#define OPENAMP_CHUNK_LEN      3072U
#define OPENAMP_SHMEM_PROBE_MAGIC 0x53484D31U

typedef struct
{
  uint32_t op;
  uint32_t value;
  uint32_t length;
  char filename[OPENAMP_FILENAME_LEN];
} OpenAmpPingRequest_t;

typedef struct
{
  uint32_t op;
  int32_t status;
  uint32_t value;
  uint32_t offset;
  uint32_t length;
  int32_t init_status;
  int32_t mount_status;
} OpenAmpSmallResponse_t;

typedef struct
{
  uint32_t op;
  int32_t status;
  uint32_t value;
  uint32_t offset;
  uint32_t length;
  int32_t init_status;
  int32_t mount_status;
  uint8_t data[OPENAMP_CHUNK_LEN];
} OpenAmpChunkResponse_t;

static volatile uint8_t g_service_created;
static volatile uint8_t g_response_ready;
static volatile uint32_t g_openamp_rx_count;
static int32_t g_openamp_init_status;
static uint8_t g_openamp_initialized;
static OpenAmpChunkResponse_t g_last_response;
static struct rpmsg_endpoint g_openamp_ping_ept;

static int OpenAmpPing_RxCallback(struct rpmsg_endpoint *ept,
                                  void *data,
                                  size_t len,
                                  uint32_t src,
                                  void *priv);
static void OpenAmpPing_ServiceDestroyCb(struct rpmsg_endpoint *ept);
static void OpenAmpPing_NewServiceCb(struct rpmsg_device *rdev,
                                     const char *name,
                                     uint32_t dest);
static int32_t OpenAmpFs_SendRequest(uint32_t op,
                                     uint32_t request_value,
                                     uint32_t request_length,
                                     const char *filename,
                                     uint32_t *reply_value);
static int32_t OpenAmpFs_SendRequestEx(uint32_t op,
                                       uint32_t request_value,
                                       uint32_t request_length,
                                       const char *filename,
                                       uint32_t *reply_value,
                                       uint8_t fast_wait);

int32_t OpenAmpFs_MasterInit(void)
{
  int32_t status;

  g_service_created = 0U;
  g_response_ready = 0U;
  g_openamp_rx_count = 0U;
  g_openamp_init_status = 0;
  memset(&g_last_response, 0, sizeof(g_last_response));

  MAILBOX_Init();
  OPENAMP_init_ept(&g_openamp_ping_ept);

  status = MX_OPENAMP_Init(RPMSG_MASTER, OpenAmpPing_NewServiceCb);
  if (status != HAL_OK)
  {
    g_openamp_init_status = status;
    return status;
  }

  g_openamp_initialized = 1U;
  g_openamp_init_status = 0;
  OPENAMP_Wait_EndPointready(&g_openamp_ping_ept);
  return 0;
}

int32_t OpenAmpFs_Ping(uint32_t request_value, uint32_t *reply_value)
{
  return OpenAmpFs_SendRequest(OPENAMP_OP_PING, request_value, 0U, NULL, reply_value);
}

int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count)
{
  return OpenAmpFs_SendRequest(OPENAMP_OP_COUNT_DAT, 0U, 0U, NULL, dat_count);
}

int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count)
{
  return OpenAmpFs_SendRequest(OPENAMP_OP_COUNT_ALL, 0U, 0U, NULL, file_count);
}

int32_t OpenAmpFs_GetFileSize(const char *filename, uint32_t *file_size)
{
  if ((filename == NULL) || (filename[0] == '\0'))
  {
    return -1;
  }

  return OpenAmpFs_SendRequest(OPENAMP_OP_FILE_SIZE, 0U, 0U, filename, file_size);
}

int32_t OpenAmpFs_ReadFileChunk(const char *filename,
                                uint32_t offset,
                                uint8_t *buffer,
                                uint16_t buffer_size,
                                uint16_t *bytes_read,
                                uint32_t *total_size)
{
  uint32_t reply_value = 0U;
  int32_t status;
  uint32_t copy_len;

  if ((filename == NULL) || (filename[0] == '\0') ||
      (buffer == NULL) || (bytes_read == NULL) || (total_size == NULL))
  {
    return -1;
  }

  *bytes_read = 0U;
  *total_size = 0U;

  status = OpenAmpFs_SendRequest(OPENAMP_OP_READ_CHUNK,
                                 offset,
                                 buffer_size,
                                 filename,
                                 &reply_value);
  *total_size = reply_value;
  if (status != 0)
  {
    return status;
  }

  copy_len = g_last_response.length;
  if (copy_len > buffer_size)
  {
    copy_len = buffer_size;
  }
  if (copy_len > OPENAMP_CHUNK_LEN)
  {
    copy_len = OPENAMP_CHUNK_LEN;
  }

  memcpy(buffer, g_last_response.data, copy_len);
  *bytes_read = (uint16_t)copy_len;
  return status;
}

int32_t OpenAmpFs_OpenFileStream(const char *filename, uint32_t offset, uint32_t *file_size)
{
  if ((filename == NULL) || (filename[0] == '\0') || (file_size == NULL))
  {
    return -1;
  }

  return OpenAmpFs_SendRequest(OPENAMP_OP_STREAM_OPEN, offset, 0U, filename, file_size);
}

int32_t OpenAmpFs_ReadFileStream(uint8_t *buffer,
                                 uint16_t buffer_size,
                                 uint16_t *bytes_read,
                                 uint32_t *offset,
                                 uint32_t *total_size)
{
  uint32_t reply_value = 0U;
  int32_t status;
  uint32_t copy_len;

  if ((buffer == NULL) || (bytes_read == NULL) || (offset == NULL) || (total_size == NULL))
  {
    return -1;
  }

  *bytes_read = 0U;
  *offset = 0U;
  *total_size = 0U;

  status = OpenAmpFs_SendRequestEx(OPENAMP_OP_STREAM_READ,
                                   0U,
                                   buffer_size,
                                   NULL,
                                   &reply_value,
                                   1U);
  *total_size = reply_value;
  *offset = g_last_response.offset;
  if (status != 0)
  {
    return status;
  }

  copy_len = g_last_response.length;
  if (copy_len > buffer_size)
  {
    copy_len = buffer_size;
  }
  if (copy_len > OPENAMP_CHUNK_LEN)
  {
    copy_len = OPENAMP_CHUNK_LEN;
  }

  memcpy(buffer, g_last_response.data, copy_len);
  *bytes_read = (uint16_t)copy_len;
  return status;
}

int32_t OpenAmpFs_CloseFileStream(void)
{
  uint32_t reply_value = 0U;

  return OpenAmpFs_SendRequest(OPENAMP_OP_STREAM_CLOSE, 0U, 0U, NULL, &reply_value);
}

int32_t OpenAmpFs_ReadFileStreamShared(uint8_t **buffer,
                                       uint16_t buffer_size,
                                       uint16_t *bytes_read,
                                       uint32_t *offset,
                                       uint32_t *total_size)
{
  uint32_t reply_value = 0U;
  int32_t status;
  uint32_t copy_len;

  if ((buffer == NULL) || (bytes_read == NULL) || (offset == NULL) || (total_size == NULL))
  {
    return -1;
  }

  *buffer = FILE_SHMEM_DATA_PTR;
  *bytes_read = 0U;
  *offset = 0U;
  *total_size = 0U;

  if (buffer_size > FILE_SHMEM_DATA_LEN)
  {
    buffer_size = (uint16_t)FILE_SHMEM_DATA_LEN;
  }

  status = OpenAmpFs_SendRequestEx(OPENAMP_OP_STREAM_READ_SHMEM,
                                   0U,
                                   buffer_size,
                                   NULL,
                                   &reply_value,
                                   1U);
  *total_size = reply_value;
  *offset = g_last_response.offset;
  if (status != 0)
  {
    return status;
  }

  copy_len = g_last_response.length;
  if (copy_len > buffer_size)
  {
    copy_len = buffer_size;
  }
  if (copy_len > FILE_SHMEM_DATA_LEN)
  {
    copy_len = FILE_SHMEM_DATA_LEN;
  }

  *bytes_read = (uint16_t)copy_len;
  return status;
}

int32_t OpenAmpFs_ProbeSharedMemory(uint32_t *probe_len, uint32_t *bad_index)
{
  uint32_t reply_value = 0U;
  uint32_t index;
  uint32_t length;
  int32_t status;

  if ((probe_len == NULL) || (bad_index == NULL))
  {
    return -1;
  }

  *probe_len = 0U;
  *bad_index = 0xFFFFFFFFU;

  status = OpenAmpFs_SendRequestEx(OPENAMP_OP_SHMEM_PROBE,
                                   0U,
                                   0U,
                                   NULL,
                                   &reply_value,
                                   1U);
  if (status != 0)
  {
    return status;
  }
  if (reply_value != OPENAMP_SHMEM_PROBE_MAGIC)
  {
    return -6;
  }

  length = g_last_response.length;
  if (length > FILE_SHMEM_DATA_LEN)
  {
    length = FILE_SHMEM_DATA_LEN;
  }

  for (index = 0U; index < length; index++)
  {
    if (FILE_SHMEM_DATA_PTR[index] != (uint8_t)(((index * 17U) + 0x5AU) & 0xFFU))
    {
      *probe_len = length;
      *bad_index = index;
      return -7;
    }
  }

  *probe_len = length;
  *bad_index = 0xFFFFFFFFU;
  return 0;
}

static int32_t OpenAmpFs_SendRequest(uint32_t op,
                                     uint32_t request_value,
                                     uint32_t request_length,
                                     const char *filename,
                                     uint32_t *reply_value)
{
  return OpenAmpFs_SendRequestEx(op, request_value, request_length, filename, reply_value, 0U);
}

static int32_t OpenAmpFs_SendRequestEx(uint32_t op,
                                       uint32_t request_value,
                                       uint32_t request_length,
                                       const char *filename,
                                       uint32_t *reply_value,
                                       uint8_t fast_wait)
{
  OpenAmpPingRequest_t request;
  uint32_t start_tick;
  uint32_t spin_count = 0U;
  int32_t status;

  if (reply_value == NULL)
  {
    return -1;
  }

  if (g_openamp_initialized == 0U)
  {
    status = OpenAmpFs_MasterInit();
    if (status != 0)
    {
      return status;
    }
  }

  if (g_service_created == 0U)
  {
    return -3;
  }

  memset(&request, 0, sizeof(request));
  request.op = op;
  request.value = request_value;
  request.length = request_length;
  if (filename != NULL)
  {
    (void)strncpy(request.filename, filename, sizeof(request.filename) - 1U);
    request.filename[sizeof(request.filename) - 1U] = '\0';
  }
  g_response_ready = 0U;

  status = OPENAMP_send(&g_openamp_ping_ept, &request, sizeof(request));
  if (status < 0)
  {
    return status;
  }

  start_tick = HAL_GetTick();
  while (g_response_ready == 0U)
  {
    OPENAMP_check_for_message();
    if ((HAL_GetTick() - start_tick) > OPENAMP_PING_TIMEOUT_MS)
    {
      return -4;
    }

    if (fast_wait == 0U)
    {
      osDelay(1);
    }
    else
    {
      spin_count++;
      if ((spin_count & 0x3FU) == 0U)
      {
        osDelay(0);
      }
    }
  }

  if (g_last_response.op != op)
  {
    return -5;
  }

  *reply_value = g_last_response.value;
  return g_last_response.status;
}

uint32_t OpenAmpFs_GetServiceCreated(void)
{
  return (uint32_t)g_service_created;
}

uint32_t OpenAmpFs_GetRxCount(void)
{
  return g_openamp_rx_count;
}

int32_t OpenAmpFs_GetInitStatus(void)
{
  return g_openamp_init_status;
}

int32_t OpenAmpFs_GetRemoteInitStatus(void)
{
  return g_last_response.init_status;
}

int32_t OpenAmpFs_GetRemoteMountStatus(void)
{
  return g_last_response.mount_status;
}

static int OpenAmpPing_RxCallback(struct rpmsg_endpoint *ept,
                                  void *data,
                                  size_t len,
                                  uint32_t src,
                                  void *priv)
{
  (void)ept;
  (void)src;
  (void)priv;

  if (data != NULL)
  {
    memset(&g_last_response, 0, sizeof(g_last_response));
    if (len >= sizeof(OpenAmpChunkResponse_t))
    {
      memcpy(&g_last_response, data, sizeof(OpenAmpChunkResponse_t));
      g_openamp_rx_count++;
      g_response_ready = 1U;
    }
    else if (len >= sizeof(OpenAmpSmallResponse_t))
    {
      memcpy(&g_last_response, data, sizeof(OpenAmpSmallResponse_t));
      g_openamp_rx_count++;
      g_response_ready = 1U;
    }
  }

  return 0;
}

static void OpenAmpPing_ServiceDestroyCb(struct rpmsg_endpoint *ept)
{
  (void)ept;
  g_service_created = 0U;
}

static void OpenAmpPing_NewServiceCb(struct rpmsg_device *rdev,
                                     const char *name,
                                     uint32_t dest)
{
  (void)rdev;

  OPENAMP_create_endpoint(&g_openamp_ping_ept,
                          name,
                          dest,
                          OpenAmpPing_RxCallback,
                          OpenAmpPing_ServiceDestroyCb);
  g_service_created = 1U;
}
