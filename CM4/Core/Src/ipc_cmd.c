#include "ipc_cmd.h"

#include <string.h>
#include "daq_engine.h"
#include "emmc_fs.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "main.h"
#include "shared_memory.h"
#include "statemachine.h"

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

static uint8_t IPC_CanStart(void)
{
  return (uint8_t)(g_daq_ctx.state == DAQ_STATE_IDLE);
}

static uint8_t IPC_CanStop(void)
{
  return (uint8_t)((g_daq_ctx.state == DAQ_STATE_PREPARING) ||
                   (g_daq_ctx.state == DAQ_STATE_ACQUIRING) ||
                   (g_daq_ctx.state == DAQ_STATE_ERROR));
}

static void IPC_PublishStatus(void)
{
  const DaqConfig_t *cfg = DAQ_GetConfig();

  LOCK_HSEM(HSEM_IPC_ID);
  SHARED_IPC_REGION->status.magic = IPC_SHARED_MAGIC;
  SHARED_IPC_REGION->status.version = IPC_SHARED_VERSION;
  SHARED_IPC_REGION->status.daq_state = (uint32_t)g_daq_ctx.state;
  SHARED_IPC_REGION->status.last_error = g_daq_ctx.last_error;
  SHARED_IPC_REGION->status.sample_rate_hz = cfg->sample_rate_hz;
  SHARED_IPC_REGION->status.channel_mask = cfg->channel_mask;
  SHARED_IPC_REGION->status.block_samples = cfg->block_samples;
  SHARED_IPC_REGION->status.samples_captured = g_daq_ctx.samples_captured;
  SHARED_IPC_REGION->status.dropped_buffers = g_daq_ctx.dropped_buffers;
  SHARED_IPC_REGION->status.bytes_queued = g_daq_ctx.bytes_queued;
  SHARED_IPC_REGION->status.bytes_written = g_daq_ctx.bytes_written;
  SHARED_IPC_REGION->status.emmc_init_status = g_cm4_emmc_init_status;
  SHARED_IPC_REGION->status.emmc_mount_status = g_cm4_emmc_mount_status;
  SHARED_IPC_REGION->status.emmc_create_status = g_cm4_emmc_create_status;
  SHARED_IPC_REGION->status.emmc_readthrough_status = g_cm4_emmc_readthrough_status;
  SHARED_IPC_REGION->status.fs_ready = (uint32_t)((g_cm4_emmc_init_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_mount_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_create_status == EMMC_FS_OK) &&
                                                  (g_cm4_emmc_readthrough_status == EMMC_FS_OK));
  memcpy((void *)SHARED_IPC_REGION->status.active_filename, cfg->filename, IPC_FILENAME_LEN);
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

static uint8_t IPC_CopyStartConfig(const IpcCommandBlock_t *cmd, DaqConfig_t *cfg)
{
  if ((cmd->payload_size < sizeof(IpcStartAcqParams_t)) || (cfg == NULL))
  {
    return 0U;
  }

  cfg->sample_rate_hz = cmd->payload.start_acq.sample_rate_hz;
  cfg->channel_mask = cmd->payload.start_acq.channel_mask;
  cfg->block_samples = cmd->payload.start_acq.block_samples;
  cfg->flags = cmd->payload.start_acq.flags;
  memcpy(cfg->filename, cmd->payload.start_acq.filename, IPC_FILENAME_LEN);
  cfg->filename[IPC_FILENAME_LEN - 1U] = '\0';

  return 1U;
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
  DaqConfig_t next_cfg;

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
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, 0U);
      break;

    case IPC_CMD_START_ACQ:
      if (IPC_CanStart() == 0U)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_REJECTED_STATE, 2U);
        break;
      }

      if (IPC_CopyStartConfig(&cmd_local, &next_cfg) == 0U)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 3U);
        break;
      }

      if (DAQ_ApplyConfig(&next_cfg) == 0U)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 4U);
        break;
      }

      g_daq_ctx.events |= DAQ_EVT_CMD_START;
      IPC_PublishStatus();
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, 0U);
      break;

    case IPC_CMD_STOP_ACQ:
      if (IPC_CanStop() == 0U)
      {
        IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_REJECTED_STATE, 5U);
        break;
      }

      g_daq_ctx.events |= DAQ_EVT_CMD_STOP;
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_OK, 0U);
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

    default:
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 6U);
      break;
  }
}

const IpcResponseBlock_t *IPC_CmdGetLastResponse(void)
{
  return &ipc_last_rsp;
}
