#include "openamp_fs.h"

#include <string.h>

#include "main.h"
#include "openamp.h"
#include "emmc_fs.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_MAGIC     0x434D3401U

// Operations for OpenAMP
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
  OpenAmpPingRequest_t request_copy;
  OpenAmpPingResponse_t response;
  EmmcFsDatSummary_t dat_summary;
  EmmcFsReadHandle_t read_handle;
  uint32_t all_file_count = 0U;
  uint32_t file_size = 0U;
  EmmcFsStatus_t fs_status;

  (void)src;
  (void)priv;

  memset(&response, 0, sizeof(response));

  if ((ept == NULL) || (data == NULL) || (len < sizeof(OpenAmpPingRequest_t)))
  {
    response.status = -1;
    response.value = 0U;
    (void)OPENAMP_send(ept, &response, sizeof(response));
    return 0;
  }

  memcpy(&request_copy, data, sizeof(request_copy));
  request_copy.filename[OPENAMP_FILENAME_LEN - 1U] = '\0';

  g_openamp_ping_rx_count++;
  response.op = request_copy.op;
  response.init_status = CM4_GetEmmcInitStatus();
  response.mount_status = CM4_GetEmmcMountStatus();

  switch (request_copy.op)
  {
    case OPENAMP_OP_PING:
      response.status = 0;
      response.value = request_copy.value + 1U + OPENAMP_PING_MAGIC;
      break;

    case OPENAMP_OP_COUNT_DAT:
      memset(&dat_summary, 0, sizeof(dat_summary));
      response.status = (int32_t)EmmcFs_CountDatFiles(&dat_summary);
      response.value = dat_summary.dat_file_count;
      break;

    case OPENAMP_OP_COUNT_ALL:
      response.status = (int32_t)EmmcFs_CountAllFiles(&all_file_count);
      response.value = all_file_count;
      break;

    case OPENAMP_OP_FILE_SIZE:
      memset(&read_handle, 0, sizeof(read_handle));
      fs_status = EmmcFs_OpenFileRead(request_copy.filename, &read_handle, &file_size);
      response.status = (int32_t)fs_status;
      response.value = file_size;
      if (fs_status == EMMC_FS_OK)
      {
        fs_status = EmmcFs_CloseFileRead(&read_handle);
        if (fs_status != EMMC_FS_OK)
        {
          response.status = (int32_t)fs_status;
        }
      }
      break;

    default:
      response.status = -1;
      response.value = 0U;
      break;
  }

  (void)OPENAMP_send(ept, &response, sizeof(response));
  return 0;
}
