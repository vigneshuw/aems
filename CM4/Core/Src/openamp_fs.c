#include "openamp_fs.h"

#include <string.h>

#include "main.h"
#include "openamp.h"
#include "emmc_fs.h"
#include "file_shmem.h"
#include "daq_engine.h"
#include "daq_shared.h"

#define OPENAMP_PING_CHAN_NAME "openamp_pingpong_demo"
#define OPENAMP_PING_MAGIC     0x434D3401U

// Operations for OpenAMP
#define OPENAMP_OP_PING        99U
#define OPENAMP_OP_COUNT_DAT   2U
#define OPENAMP_OP_COUNT_ALL   3U
#define OPENAMP_OP_LIST_FILES  4U
#define OPENAMP_OP_FILE_SIZE   5U
#define OPENAMP_OP_DELETE_LOGS 96U
#define OPENAMP_OP_DELETE_FILE 97U
#define OPENAMP_OP_READ_CHUNK  7U
#define OPENAMP_OP_STREAM_OPEN 80U
#define OPENAMP_OP_STREAM_READ 81U
#define OPENAMP_OP_STREAM_CLOSE 82U
#define OPENAMP_OP_STREAM_READ_SHMEM 83U
#define OPENAMP_OP_SHMEM_PROBE 84U
#define OPENAMP_OP_DAQ_GET_CAL 85U
#define OPENAMP_OP_DAQ_RUN_CAL 86U
#define OPENAMP_OP_DAQ_STATUS  90U
#define OPENAMP_OP_DAQ_START_LOG 91U
#define OPENAMP_OP_DAQ_START_STREAM 92U
#define OPENAMP_OP_DAQ_READ_SHMEM 93U
#define OPENAMP_OP_DAQ_STOP 94U
#define OPENAMP_OP_DAQ_STOP_CLOSE 95U
#define OPENAMP_FILENAME_LEN   64U
#define OPENAMP_CHUNK_LEN      3072U
#define OPENAMP_SHMEM_PROBE_LEN 256U
#define OPENAMP_SHMEM_PROBE_MAGIC 0x53484D31U

typedef struct
{
  uint32_t op;
  uint32_t value;
  uint32_t length;
  uint32_t arg0;
  uint32_t arg1;
  uint32_t arg2;
  uint32_t arg3;
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
  uint32_t arg0;
  uint32_t arg1;
  uint32_t arg2;
  uint32_t arg3;
  uint32_t arg4;
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
  uint32_t arg0;
  uint32_t arg1;
  uint32_t arg2;
  uint32_t arg3;
  uint32_t arg4;
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
  DaqConfig_t daq_cfg;
  DaqStatus_t daq_status;
  DaqCalibration_t daq_calibration;
  uint8_t *daq_buffer = NULL;
  uint32_t daq_samples_read = 0U;

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
  response.arg0 = (uint32_t)CM4_GetAdcDeviceId();
  response.arg1 = CM4_GetEmmcMountDiagStage();
  response.arg2 = CM4_GetEmmcMountDiagMountFresult();
  response.arg3 = CM4_GetEmmcMountDiagMkfsFresult();
  response.arg4 = CM4_GetEmmcMountDiagPostMountFresult();

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

    case OPENAMP_OP_LIST_FILES:
      response.status = (int32_t)EmmcFs_ListFiles((char *)FILE_SHMEM_DATA_PTR,
                                                  FILE_SHMEM_DATA_LEN,
                                                  &all_file_count);
      response.value = all_file_count;
      response.length = (response.status == 0) ? all_file_count : 0U;
      break;

    case OPENAMP_OP_DELETE_LOGS:
      response.status = (int32_t)EmmcFs_DeleteLogFiles(&all_file_count);
      response.value = all_file_count;
      break;

    case OPENAMP_OP_DELETE_FILE:
      response.status = (int32_t)EmmcFs_DeleteFileIfExists(request_copy.filename, &all_file_count);
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
      chunk_response.arg0 = (uint32_t)CM4_GetAdcDeviceId();

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
      chunk_response.arg0 = (uint32_t)CM4_GetAdcDeviceId();
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

    case OPENAMP_OP_DAQ_GET_CAL:
      memset(&daq_calibration, 0, sizeof(daq_calibration));
      DAQ_GetOffsetCalibration(&daq_calibration);
      response.status = 0;
      response.value = (uint32_t)daq_calibration.offset[0];
      response.offset = (uint32_t)daq_calibration.offset[1];
      response.length = (uint32_t)daq_calibration.offset[2];
      response.arg1 = (uint32_t)daq_calibration.offset[3];
      response.arg2 = (uint32_t)daq_calibration.offset[4];
      response.arg3 = (uint32_t)daq_calibration.offset[5];
      response.arg4 = daq_calibration.samples_averaged;
      break;

    case OPENAMP_OP_DAQ_RUN_CAL:
      memset(&daq_calibration, 0, sizeof(daq_calibration));
      response.status = DAQ_RunOffsetCalibration(&daq_calibration);
      response.value = (uint32_t)daq_calibration.offset[0];
      response.offset = (uint32_t)daq_calibration.offset[1];
      response.length = (uint32_t)daq_calibration.offset[2];
      response.arg1 = (uint32_t)daq_calibration.offset[3];
      response.arg2 = (uint32_t)daq_calibration.offset[4];
      response.arg3 = (uint32_t)daq_calibration.offset[5];
      response.arg4 = daq_calibration.samples_averaged;
      break;

    case OPENAMP_OP_DAQ_STATUS:
      memset(&daq_status, 0, sizeof(daq_status));
      DAQ_GetStatus(&daq_status);
      response.status = 0;
      response.value = daq_status.state;
      response.offset = daq_status.samples_captured;
      response.length = daq_status.dropped_buffers;
      response.arg0 = daq_status.mode;
      response.arg1 = daq_status.last_error;
      response.arg2 = (uint32_t)daq_status.bytes_written;
      response.arg3 = (uint32_t)(daq_status.bytes_written >> 32);
      response.arg4 = daq_status.adc_ready_pending;
      break;

    // Start and Start logging has the same starting point
    case OPENAMP_OP_DAQ_START_LOG:
    case OPENAMP_OP_DAQ_START_STREAM:
      // Parse DAQ parameters
      memset(&daq_cfg, 0, sizeof(daq_cfg));
      daq_cfg.sample_rate_hz = request_copy.value;
      daq_cfg.block_samples = request_copy.length;
      daq_cfg.channel_mask = request_copy.arg0;
      daq_cfg.flags = request_copy.arg1;
      (void)strncpy(daq_cfg.filename, request_copy.filename, sizeof(daq_cfg.filename) - 1U);
      daq_cfg.filename[sizeof(daq_cfg.filename) - 1U] = '\0';

      // Decide on the logging process
      if (request_copy.op == OPENAMP_OP_DAQ_START_LOG)
      {
        response.status = (DAQ_StartLogging(&daq_cfg) != 0U) ? 0 : DAQ_GetLastOpStatus();
      }
      else
      {
        response.status = (DAQ_StartStreaming(&daq_cfg) != 0U) ? 0 : DAQ_GetLastOpStatus();
      }
      response.value = (uint32_t)g_daq_ctx.state;
      break;

    case OPENAMP_OP_DAQ_READ_SHMEM:
      requested_len = (request_copy.length > FILE_SHMEM_DATA_LEN) ?
                      (uint16_t)FILE_SHMEM_DATA_LEN :
                      (uint16_t)request_copy.length;
      if (requested_len == 0U)
      {
        requested_len = (uint16_t)FILE_SHMEM_DATA_LEN;
      }

      if (DAQ_ReadStreamBlockShared(&daq_buffer,
                                    requested_len,
                                    &bytes_read,
                                    &daq_samples_read) == 0U)
      {
        response.status = -11;
      }
      else
      {
        response.status = 0;
      }
      response.value = daq_samples_read;
      response.offset = g_daq_ctx.samples_captured;
      response.length = bytes_read;
      break;

    case OPENAMP_OP_DAQ_STOP:
      DAQ_Stop();
      response.status = 0;
      response.value = (uint32_t)g_daq_ctx.state;
      break;

    case OPENAMP_OP_DAQ_STOP_CLOSE:
      response.status = (DAQ_StopAndClose() != 0U) ? 0 : DAQ_GetLastOpStatus();
      response.value = (uint32_t)g_daq_ctx.state;
      response.offset = g_daq_ctx.samples_captured;
      response.length = g_daq_ctx.dropped_buffers;
      response.arg0 = (uint32_t)g_daq_ctx.last_error;
      response.arg1 = (uint32_t)g_daq_ctx.bytes_written;
      response.arg2 = (uint32_t)(g_daq_ctx.bytes_written >> 32);
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
