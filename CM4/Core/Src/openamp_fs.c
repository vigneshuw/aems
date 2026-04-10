#include "openamp_fs.h"

#include <string.h>

#include "main.h"
#include "openamp.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_MAGIC     0x434D3401U

typedef struct
{
  uint32_t value;
} OpenAmpPingRequest_t;

typedef struct
{
  int32_t status;
  uint32_t value;
  int32_t init_status;
  int32_t mount_status;
} OpenAmpPingResponse_t;

static struct rpmsg_endpoint g_openamp_ping_ept;
static volatile uint32_t g_openamp_ping_rx_count;
static volatile int32_t g_openamp_ping_init_status;

static int OpenAmpPing_RxCallback(struct rpmsg_endpoint *ept,
                                  void *data,
                                  size_t len,
                                  uint32_t src,
                                  void *priv);

int32_t OpenAmpFs_RemoteInit(void)
{
  int32_t status;

  MAILBOX_Init();

  status = MX_OPENAMP_Init(RPMSG_REMOTE, NULL);
  if (status != 0)
  {
    g_openamp_ping_init_status = status;
    return status;
  }

  status = OPENAMP_create_endpoint(&g_openamp_ping_ept,
                                   OPENAMP_PING_CHAN_NAME,
                                   RPMSG_ADDR_ANY,
                                   OpenAmpPing_RxCallback,
                                   NULL);
  g_openamp_ping_init_status = status;
  return status;
}

void OpenAmpFs_RemotePoll(void)
{
  OPENAMP_check_for_message();
}

int32_t OpenAmpFs_GetRemoteInitStatus(void)
{
  return g_openamp_ping_init_status;
}

uint32_t OpenAmpFs_GetRemoteRxCount(void)
{
  return g_openamp_ping_rx_count;
}

static int OpenAmpPing_RxCallback(struct rpmsg_endpoint *ept,
                                  void *data,
                                  size_t len,
                                  uint32_t src,
                                  void *priv)
{
  const OpenAmpPingRequest_t *request = (const OpenAmpPingRequest_t *)data;
  OpenAmpPingResponse_t response;

  (void)src;
  (void)priv;

  memset(&response, 0, sizeof(response));

  if ((ept == NULL) || (request == NULL) || (len < sizeof(OpenAmpPingRequest_t)))
  {
    response.status = -1;
    response.value = 0U;
    (void)OPENAMP_send(ept, &response, sizeof(response));
    return 0;
  }

  g_openamp_ping_rx_count++;
  response.status = 0;
  response.value = request->value + 1U + OPENAMP_PING_MAGIC;
  response.init_status = CM4_GetEmmcInitStatus();
  response.mount_status = CM4_GetEmmcMountStatus();
  (void)OPENAMP_send(ept, &response, sizeof(response));
  return 0;
}
