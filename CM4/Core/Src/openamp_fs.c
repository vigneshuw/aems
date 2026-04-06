#include "openamp_fs.h"

#include <string.h>

#include "openamp.h"
#include "emmc_fs.h"

#define OPENAMP_FS_CHAN_NAME "aems.fs"

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

static struct rpmsg_endpoint g_openamp_fs_ept;

static int OpenAmpFs_RxCallback(struct rpmsg_endpoint *ept,
                                void *data,
                                size_t len,
                                uint32_t src,
                                void *priv);

int32_t OpenAmpFs_RemoteInit(void)
{
  int32_t status;

  status = MX_OPENAMP_Init(RPMSG_REMOTE, NULL);
  if (status != HAL_OK)
  {
    return status;
  }

  status = OPENAMP_create_endpoint(&g_openamp_fs_ept,
                                   OPENAMP_FS_CHAN_NAME,
                                   RPMSG_ADDR_ANY,
                                   OpenAmpFs_RxCallback,
                                   NULL);
  return status;
}

void OpenAmpFs_RemotePoll(void)
{
  OPENAMP_check_for_message();
}

static int OpenAmpFs_RxCallback(struct rpmsg_endpoint *ept,
                                void *data,
                                size_t len,
                                uint32_t src,
                                void *priv)
{
  const OpenAmpFsRequest_t *request = (const OpenAmpFsRequest_t *)data;
  OpenAmpFsResponse_t response;
  EmmcFsDatSummary_t summary;
  uint32_t all_count = 0U;

  (void)src;
  (void)priv;

  memset(&response, 0, sizeof(response));

  if ((ept == NULL) || (request == NULL) || (len < sizeof(OpenAmpFsRequest_t)))
  {
    response.status = (int32_t)EMMC_FS_ERR_PARAM;
    (void)OPENAMP_send(ept, &response, sizeof(response));
    return 0;
  }

  response.op = request->op;

  switch ((OpenAmpFsOp_t)request->op)
  {
    case OPENAMP_FS_OP_COUNT_DAT:
      memset(&summary, 0, sizeof(summary));
      response.status = (int32_t)EmmcFs_CountDatFiles(&summary);
      response.value = summary.dat_file_count;
      break;

    case OPENAMP_FS_OP_COUNT_ALL:
      response.status = (int32_t)EmmcFs_CountAllFiles(&all_count);
      response.value = all_count;
      break;

    default:
      response.status = (int32_t)EMMC_FS_ERR_PARAM;
      break;
  }

  (void)OPENAMP_send(ept, &response, sizeof(response));
  return 0;
}
