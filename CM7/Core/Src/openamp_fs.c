#include "openamp_fs.h"

#include <string.h>

#include "cmsis_os.h"
#include "openamp.h"

#define OPENAMP_FS_CHAN_NAME "aems.fs"
#define OPENAMP_FS_TIMEOUT_MS 3000U

typedef enum
{
  OPENAMP_FS_OP_NONE = 0,
  OPENAMP_FS_OP_COUNT_DAT = 1,
  OPENAMP_FS_OP_COUNT_ALL = 2
} OpenAmpFsOp_t;

typedef struct
{
  uint32_t op;
} OpenAmpFsRequest_t;

typedef struct
{
  uint32_t op;
  int32_t status;
  uint32_t value;
} OpenAmpFsResponse_t;

static volatile uint8_t g_service_created;
static volatile uint8_t g_response_ready;
static uint8_t g_openamp_initialized;
static OpenAmpFsResponse_t g_last_response;
static struct rpmsg_endpoint g_openamp_fs_ept;

static int OpenAmpFs_RxCallback(struct rpmsg_endpoint *ept,
                                void *data,
                                size_t len,
                                uint32_t src,
                                void *priv);
static void OpenAmpFs_ServiceDestroyCb(struct rpmsg_endpoint *ept);
static void OpenAmpFs_NewServiceCb(struct rpmsg_device *rdev,
                                   const char *name,
                                   uint32_t dest);
static int32_t OpenAmpFs_SendRequest(uint32_t op, uint32_t *value_out);

int32_t OpenAmpFs_MasterInit(void)
{
  int32_t status;

  g_service_created = 0U;
  g_response_ready = 0U;
  g_openamp_initialized = 0U;
  memset(&g_last_response, 0, sizeof(g_last_response));

  OPENAMP_init_ept(&g_openamp_fs_ept);

  status = MX_OPENAMP_Init(RPMSG_MASTER, OpenAmpFs_NewServiceCb);
  if (status != HAL_OK)
  {
    return status;
  }

  g_openamp_initialized = 1U;
  OPENAMP_Wait_EndPointready(&g_openamp_fs_ept);
  return 0;
}

int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count)
{
  return OpenAmpFs_SendRequest((uint32_t)OPENAMP_FS_OP_COUNT_DAT, dat_count);
}

int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count)
{
  return OpenAmpFs_SendRequest((uint32_t)OPENAMP_FS_OP_COUNT_ALL, file_count);
}

static int32_t OpenAmpFs_SendRequest(uint32_t op, uint32_t *value_out)
{
  OpenAmpFsRequest_t request;
  uint32_t start_tick;
  int32_t status;

  if (value_out == NULL)
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

  if (!g_service_created)
  {
    return -2;
  }

  memset(&request, 0, sizeof(request));
  request.op = op;
  g_response_ready = 0U;

  status = OPENAMP_send(&g_openamp_fs_ept, &request, sizeof(request));
  if (status < 0)
  {
    return status;
  }

  start_tick = HAL_GetTick();
  while (!g_response_ready)
  {
    OPENAMP_check_for_message();
    if ((HAL_GetTick() - start_tick) > OPENAMP_FS_TIMEOUT_MS)
    {
      return -3;
    }
    osDelay(1);
  }

  if (g_last_response.op != op)
  {
    return -4;
  }

  *value_out = g_last_response.value;
  return g_last_response.status;
}

static int OpenAmpFs_RxCallback(struct rpmsg_endpoint *ept,
                                void *data,
                                size_t len,
                                uint32_t src,
                                void *priv)
{
  (void)ept;
  (void)src;
  (void)priv;

  if ((data != NULL) && (len >= sizeof(OpenAmpFsResponse_t)))
  {
    memcpy(&g_last_response, data, sizeof(g_last_response));
    g_response_ready = 1U;
  }

  return 0;
}

static void OpenAmpFs_ServiceDestroyCb(struct rpmsg_endpoint *ept)
{
  (void)ept;
  g_service_created = 0U;
}

static void OpenAmpFs_NewServiceCb(struct rpmsg_device *rdev,
                                   const char *name,
                                   uint32_t dest)
{
  (void)rdev;

  OPENAMP_create_endpoint(&g_openamp_fs_ept,
                          name,
                          dest,
                          OpenAmpFs_RxCallback,
                          OpenAmpFs_ServiceDestroyCb);
  g_service_created = 1U;
}
