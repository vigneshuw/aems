#include "ipc_cmd.h"

#include <string.h>
#include "emmc_fs.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "main.h"
#include "shared_memory.h"

extern volatile int32_t g_cm4_emmc_init_status;
extern volatile int32_t g_cm4_emmc_mount_status;
extern volatile int32_t g_cm4_emmc_create_status;
extern volatile int32_t g_cm4_emmc_readthrough_status;

static IpcResponseBlock_t ipc_last_rsp;
static struct
{
  EmmcFsReadHandle_t handle;
  uint32_t seq;
  uint32_t offset;
  uint32_t total_size;
  uint8_t active;
} ipc_stream_ctx;
ALIGN_32BYTES(static uint8_t ipc_stream_buf[IPC_CHUNK_BUFFER_SIZE]);

static void IPC_PublishStatus(void)
{
  LOCK_HSEM(HSEM_IPC_ID);
  SHARED_IPC_REGION->status.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->status.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->status.daq_state = 0U;
  SHARED_IPC_REGION->status.last_error = 0U;
  SHARED_IPC_REGION->status.sample_rate_hz = 0U;
  SHARED_IPC_REGION->status.channel_mask = 0U;
  SHARED_IPC_REGION->status.block_samples = 0U;
  SHARED_IPC_REGION->status.samples_captured = 0U;
  SHARED_IPC_REGION->status.dropped_buffers = 0U;
  SHARED_IPC_REGION->status.bytes_queued = 0U;
  SHARED_IPC_REGION->status.bytes_written = 0U;
  SHARED_IPC_REGION->status.emmc_busy = (uint32_t)(ipc_stream_ctx.active != 0U);
  SHARED_IPC_REGION->status.emmc_init_status = g_cm4_emmc_init_status;
  SHARED_IPC_REGION->status.emmc_mount_status = g_cm4_emmc_mount_status;
  SHARED_IPC_REGION->status.emmc_create_status = g_cm4_emmc_create_status;
  SHARED_IPC_REGION->status.emmc_readthrough_status = g_cm4_emmc_readthrough_status;
  SHARED_IPC_REGION->status.fs_ready = (uint32_t)((g_cm4_emmc_init_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_mount_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_create_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_readthrough_status == EMMC_FS_OK));
  memset((void *)SHARED_IPC_REGION->status.active_filename, 0, IPC_FILENAME_LEN);
  if (ipc_stream_ctx.active != 0U)
  {
    memcpy((void *)SHARED_IPC_REGION->status.active_filename,
           (const void *)SHARED_IPC_REGION->stream.filename,
           IPC_FILENAME_LEN);
  }
  UNLOCK_HSEM(HSEM_IPC_ID);
}

static void IPC_WriteResponse(uint32_t seq, uint32_t cmd, uint32_t result, uint32_t error)
{
  ipc_last_rsp.magic = IPC_SHARED_MAGIC;
  ipc_last_rsp.version = IPC_SHARED_VERSION;
  ipc_last_rsp.ready = 1U;
  ipc_last_rsp.seq = seq;
  ipc_last_rsp.cmd = cmd;
  ipc_last_rsp.result = result;
  ipc_last_rsp.error = error;

  LOCK_HSEM(HSEM_IPC_ID);
  memcpy((void *)&SHARED_IPC_REGION->rsp, &ipc_last_rsp, sizeof(ipc_last_rsp));
  UNLOCK_HSEM(HSEM_IPC_ID);
}

static uint8_t IPC_ReadPendingCommand(IpcCommandBlock_t *cmd_local)
{
  uint8_t has_cmd = 0U;

  LOCK_HSEM(HSEM_IPC_ID);
  if (SHARED_IPC_REGION->cmd.pending != 0U)
  {
    memcpy(cmd_local, (const void *)&SHARED_IPC_REGION->cmd, sizeof(*cmd_local));
    SHARED_IPC_REGION->cmd.pending = 0U;
    has_cmd = 1U;
  }
  UNLOCK_HSEM(HSEM_IPC_ID);

  return has_cmd;
}

static void IPC_StreamResetShared(void)
{
  LOCK_HSEM(HSEM_IPC_ID);
  SHARED_IPC_REGION->stream.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->stream.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->stream.active = 0U;
  SHARED_IPC_REGION->stream.seq = 0U;
  SHARED_IPC_REGION->stream.state = IPC_STREAM_EMPTY;
  SHARED_IPC_REGION->stream.total_size = 0U;
  SHARED_IPC_REGION->stream.offset = 0U;
  SHARED_IPC_REGION->stream.length = 0U;
  SHARED_IPC_REGION->stream.error = 0U;
  memset((void *)SHARED_IPC_REGION->stream.filename, 0, IPC_FILENAME_LEN);
  UNLOCK_HSEM(HSEM_IPC_ID);
}

static void IPC_StreamService(void)
{
  uint16_t bytes_read;

  if (ipc_stream_ctx.active == 0U)
  {
    return;
  }

  LOCK_HSEM(HSEM_IPC_ID);
  if (SHARED_IPC_REGION->stream.state != IPC_STREAM_EMPTY)
  {
    UNLOCK_HSEM(HSEM_IPC_ID);
    return;
  }
  UNLOCK_HSEM(HSEM_IPC_ID);

  bytes_read = 0U;
  if (EmmcFs_ReadFileNext(&ipc_stream_ctx.handle, ipc_stream_buf, sizeof(ipc_stream_buf), &bytes_read) != EMMC_FS_OK)
  {
    LOCK_HSEM(HSEM_IPC_ID);
    SHARED_IPC_REGION->stream.state = IPC_STREAM_ERROR;
    SHARED_IPC_REGION->stream.error = 1U;
    SHARED_IPC_REGION->stream.active = 0U;
    UNLOCK_HSEM(HSEM_IPC_ID);
    (void)EmmcFs_CloseFileRead(&ipc_stream_ctx.handle);
    ipc_stream_ctx.active = 0U;
    return;
  }

  if (bytes_read == 0U)
  {
    LOCK_HSEM(HSEM_IPC_ID);
    SHARED_IPC_REGION->stream.state = IPC_STREAM_DONE;
    SHARED_IPC_REGION->stream.error = 0U;
    SHARED_IPC_REGION->stream.active = 0U;
    UNLOCK_HSEM(HSEM_IPC_ID);
    (void)EmmcFs_CloseFileRead(&ipc_stream_ctx.handle);
    ipc_stream_ctx.active = 0U;
    return;
  }

  LOCK_HSEM(HSEM_IPC_ID);
  memcpy((void *)SHARED_IPC_REGION->chunk_buffer, ipc_stream_buf, bytes_read);
  SHARED_IPC_REGION->stream.offset = ipc_stream_ctx.offset;
  SHARED_IPC_REGION->stream.length = bytes_read;
  SHARED_IPC_REGION->stream.state = IPC_STREAM_READY;
  UNLOCK_HSEM(HSEM_IPC_ID);

  ipc_stream_ctx.offset += bytes_read;
}

void IPC_CmdInit(void)
{
  LOCK_HSEM(HSEM_IPC_ID);
  memset((void *)SHARED_IPC_REGION, 0, sizeof(*SHARED_IPC_REGION));
  SHARED_IPC_REGION->cmd.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->cmd.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->rsp.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->rsp.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->status.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->status.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->stream.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->stream.version = IPC_SHARED_VERSION;
  UNLOCK_HSEM(HSEM_IPC_ID);

  memset(&ipc_stream_ctx, 0, sizeof(ipc_stream_ctx));
  IPC_StreamResetShared();
  IPC_WriteResponse(0U, IPC_CMD_NONE, IPC_CMD_RES_OK, 0U);
  IPC_PublishStatus();
}

void IPC_CmdService(void)
{
  IpcCommandBlock_t cmd_local;

  IPC_PublishStatus();
  IPC_StreamService();

  if (IPC_ReadPendingCommand(&cmd_local) == 0U)
  {
    return;
  }

  if ((cmd_local.magic != IPC_SHARED_MAGIC) || (cmd_local.version != IPC_SHARED_VERSION))
  {
    IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 1U);
    return;
  }

  switch (cmd_local.cmd)
  {
    case IPC_CMD_PING:
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, 0U);
      break;

    case IPC_CMD_GET_STATUS:
    case IPC_CMD_START_ACQ:
    case IPC_CMD_STOP_ACQ:
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 9U);
      break;

    case IPC_CMD_STREAM_FILE:
      if (ipc_stream_ctx.active != 0U)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_BUSY, 7U);
        break;
      }

      if (EmmcFs_OpenFileRead(cmd_local.payload.stream_file.filename,
                              &ipc_stream_ctx.handle,
                              &ipc_stream_ctx.total_size) != EMMC_FS_OK)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 8U);
        break;
      }

      ipc_stream_ctx.active = 1U;
      ipc_stream_ctx.seq = cmd_local.seq;
      ipc_stream_ctx.offset = 0U;

      LOCK_HSEM(HSEM_IPC_ID);
      SHARED_IPC_REGION->stream.magic = IPC_SHARED_MAGIC;
      SHARED_IPC_REGION->stream.version = IPC_SHARED_VERSION;
      SHARED_IPC_REGION->stream.active = 1U;
      SHARED_IPC_REGION->stream.seq = cmd_local.seq;
      SHARED_IPC_REGION->stream.state = IPC_STREAM_EMPTY;
      SHARED_IPC_REGION->stream.total_size = ipc_stream_ctx.total_size;
      SHARED_IPC_REGION->stream.offset = 0U;
      SHARED_IPC_REGION->stream.length = 0U;
      SHARED_IPC_REGION->stream.error = 0U;
      memset((void *)SHARED_IPC_REGION->stream.filename, 0, IPC_FILENAME_LEN);
      memcpy((void *)SHARED_IPC_REGION->stream.filename,
             cmd_local.payload.stream_file.filename,
             IPC_FILENAME_LEN);
      UNLOCK_HSEM(HSEM_IPC_ID);

      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, 0U);
      break;

    case IPC_CMD_COUNT_DAT_FILES:
    {
      EmmcFsDatSummary_t summary;
      EmmcFsStatus_t fs_status;

      memset(&summary, 0, sizeof(summary));
      fs_status = EmmcFs_CountDatFiles(&summary);
      if (fs_status != EMMC_FS_OK)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, (uint32_t)fs_status);
      }
      else
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, summary.dat_file_count);
      }
      break;
    }

    case IPC_CMD_COUNT_ALL_FILES:
    {
      uint32_t file_count = 0U;
      EmmcFsStatus_t fs_status;

      fs_status = EmmcFs_CountAllFiles(&file_count);
      if (fs_status != EMMC_FS_OK)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, (uint32_t)fs_status);
      }
      else
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, file_count);
      }
      break;
    }

    default:
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 6U);
      break;
  }
}

const IpcResponseBlock_t *IPC_CmdGetLastResponse(void)
{
  return &ipc_last_rsp;
}
