#include "openamp_fs.h"

#include <string.h>

#include "cmsis_os.h"
#include "openamp.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_TIMEOUT_MS 3000U

#define OPENAMP_OP_PING        99U
#define OPENAMP_OP_COUNT_DAT   2U
#define OPENAMP_OP_COUNT_ALL   3U
#define OPENAMP_OP_FILE_SIZE   5U
#define OPENAMP_FILENAME_LEN   64U

typedef struct
{
  uint32_t op;
  uint32_t value;
  char filename[OPENAMP_FILENAME_LEN];
} OpenAmpPingRequest_t;

typedef struct
{
  uint32_t op;
  int32_t status;
  uint32_t value;
  int32_t init_status;
  int32_t mount_status;
} OpenAmpPingResponse_t;

static volatile uint8_t g_service_created;
static volatile uint8_t g_response_ready;
static volatile uint32_t g_openamp_rx_count;
static int32_t g_openamp_init_status;
static uint8_t g_openamp_initialized;
static OpenAmpPingResponse_t g_last_response;
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
                                     const char *filename,
                                     uint32_t *reply_value);

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
  return OpenAmpFs_SendRequest(OPENAMP_OP_PING, request_value, NULL, reply_value);
}

int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count)
{
  return OpenAmpFs_SendRequest(OPENAMP_OP_COUNT_DAT, 0U, NULL, dat_count);
}

int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count)
{
  return OpenAmpFs_SendRequest(OPENAMP_OP_COUNT_ALL, 0U, NULL, file_count);
}

int32_t OpenAmpFs_GetFileSize(const char *filename, uint32_t *file_size)
{
  if ((filename == NULL) || (filename[0] == '\0'))
  {
    return -1;
  }

  return OpenAmpFs_SendRequest(OPENAMP_OP_FILE_SIZE, 0U, filename, file_size);
}

static int32_t OpenAmpFs_SendRequest(uint32_t op,
                                     uint32_t request_value,
                                     const char *filename,
                                     uint32_t *reply_value)
{
  OpenAmpPingRequest_t request;
  uint32_t start_tick;
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

  request.op = op;
  request.value = request_value;
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
    osDelay(1);
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

  if ((data != NULL) && (len >= sizeof(OpenAmpPingResponse_t)))
  {
    memcpy(&g_last_response, data, sizeof(g_last_response));
    g_openamp_rx_count++;
    g_response_ready = 1U;
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
