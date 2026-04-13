#include "openamp_fs.h"

#include <string.h>

#include "main.h"
#include "openamp.h"
#include "emmc_fs.h"
#include "file_shmem.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_MAGIC     0x434D3401U

// Operations for OpenAMP
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
#define OPENAMP_SHMEM_PROBE_LEN 256U
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

static struct rpmsg_endpoint g_openamp_ping_ept;
static volatile uint32_t g_openamp_ping_rx_count;
static volatile int32_t g_openamp_ping_init_status;
static EmmcFsReadHandle_t g_stream_handle;
static uint32_t g_stream_offset;
static uint32_t g_stream_total_size;

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
  OpenAmpSmallResponse_t response;
  OpenAmpChunkResponse_t chunk_response;
  EmmcFsDatSummary_t dat_summary;
  EmmcFsReadHandle_t read_handle;
  uint32_t all_file_count = 0U;
  uint32_t file_size = 0U;
  uint16_t bytes_read = 0U;
  uint16_t requested_len = 0U;
  uint32_t index;
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

    case OPENAMP_OP_READ_CHUNK:
      requested_len = (request_copy.length > OPENAMP_CHUNK_LEN) ?
                      OPENAMP_CHUNK_LEN :
                      (uint16_t)request_copy.length;
      if (requested_len == 0U)
      {
        requested_len = OPENAMP_CHUNK_LEN;
      }

      memset(&chunk_response, 0, sizeof(chunk_response));
      chunk_response.op = request_copy.op;
      chunk_response.init_status = CM4_GetEmmcInitStatus();
      chunk_response.mount_status = CM4_GetEmmcMountStatus();

      fs_status = EmmcFs_ReadFileChunk(request_copy.filename,
                                       request_copy.value,
                                       chunk_response.data,
                                       requested_len,
                                       &bytes_read,
                                       &file_size);
      chunk_response.status = (int32_t)fs_status;
      chunk_response.value = file_size;
      chunk_response.offset = request_copy.value;
      chunk_response.length = bytes_read;
      (void)OPENAMP_send(ept, &chunk_response, sizeof(chunk_response));
      return 0;

    case OPENAMP_OP_STREAM_OPEN:
      if (g_stream_handle.is_open != 0U)
      {
        (void)EmmcFs_CloseFileRead(&g_stream_handle);
      }

      memset(&g_stream_handle, 0, sizeof(g_stream_handle));
      g_stream_offset = 0U;
      g_stream_total_size = 0U;

      fs_status = EmmcFs_OpenFileRead(request_copy.filename,
                                      &g_stream_handle,
                                      &g_stream_total_size);
      if ((fs_status == EMMC_FS_OK) && (request_copy.value > g_stream_total_size))
      {
        fs_status = EMMC_FS_ERR_PARAM;
      }
      if ((fs_status == EMMC_FS_OK) && (request_copy.value > 0U))
      {
        fs_status = EmmcFs_SeekFileRead(&g_stream_handle, request_copy.value);
      }
      if (fs_status != EMMC_FS_OK)
      {
        (void)EmmcFs_CloseFileRead(&g_stream_handle);
        g_stream_offset = 0U;
        g_stream_total_size = 0U;
      }
      else
      {
        g_stream_offset = request_copy.value;
      }

      response.status = (int32_t)fs_status;
      response.value = g_stream_total_size;
      response.offset = g_stream_offset;
      break;

    case OPENAMP_OP_STREAM_READ:
      requested_len = (request_copy.length > OPENAMP_CHUNK_LEN) ?
                      OPENAMP_CHUNK_LEN :
                      (uint16_t)request_copy.length;
      if (requested_len == 0U)
      {
        requested_len = OPENAMP_CHUNK_LEN;
      }

      memset(&chunk_response, 0, sizeof(chunk_response));
      chunk_response.op = request_copy.op;
      chunk_response.init_status = CM4_GetEmmcInitStatus();
      chunk_response.mount_status = CM4_GetEmmcMountStatus();
      chunk_response.value = g_stream_total_size;
      chunk_response.offset = g_stream_offset;

      if (g_stream_handle.is_open == 0U)
      {
        chunk_response.status = (int32_t)EMMC_FS_ERR_PARAM;
      }
      else
      {
        fs_status = EmmcFs_ReadFileNext(&g_stream_handle,
                                        chunk_response.data,
                                        requested_len,
                                        &bytes_read);
        chunk_response.status = (int32_t)fs_status;
        chunk_response.length = bytes_read;
        if (fs_status == EMMC_FS_OK)
        {
          g_stream_offset += bytes_read;
        }
      }

      (void)OPENAMP_send(ept, &chunk_response, sizeof(chunk_response));
      return 0;

    case OPENAMP_OP_STREAM_READ_SHMEM:
      requested_len = (request_copy.length > FILE_SHMEM_DATA_LEN) ?
                      (uint16_t)FILE_SHMEM_DATA_LEN :
                      (uint16_t)request_copy.length;
      if (requested_len == 0U)
      {
        requested_len = (uint16_t)FILE_SHMEM_DATA_LEN;
      }

      response.value = g_stream_total_size;
      response.offset = g_stream_offset;

      if (g_stream_handle.is_open == 0U)
      {
        response.status = (int32_t)EMMC_FS_ERR_PARAM;
      }
      else
      {
        fs_status = EmmcFs_ReadFileNext(&g_stream_handle,
                                        FILE_SHMEM_DATA_PTR,
                                        requested_len,
                                        &bytes_read);
        response.status = (int32_t)fs_status;
        response.length = bytes_read;
        if (fs_status == EMMC_FS_OK)
        {
          g_stream_offset += bytes_read;
        }
      }
      break;

    case OPENAMP_OP_SHMEM_PROBE:
      for (index = 0U; index < OPENAMP_SHMEM_PROBE_LEN; index++)
      {
        FILE_SHMEM_DATA_PTR[index] = (uint8_t)(((index * 17U) + 0x5AU) & 0xFFU);
      }

      response.status = 0;
      response.value = OPENAMP_SHMEM_PROBE_MAGIC;
      response.offset = FILE_SHMEM_DATA_ADDR;
      response.length = OPENAMP_SHMEM_PROBE_LEN;
      break;

    case OPENAMP_OP_STREAM_CLOSE:
      fs_status = EmmcFs_CloseFileRead(&g_stream_handle);
      response.status = (int32_t)fs_status;
      response.value = g_stream_total_size;
      response.offset = g_stream_offset;
      g_stream_offset = 0U;
      g_stream_total_size = 0U;
      break;

    default:
      response.status = -1;
      response.value = 0U;
      break;
  }

  (void)OPENAMP_send(ept, &response, sizeof(response));
  return 0;
}
