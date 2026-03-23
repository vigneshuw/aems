#include "ipc_cmd.h"

#include <string.h>
#include "daq_engine.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "main.h"
#include "shared_memory.h"
#include "statemachine.h"

static IpcResponseBlock_t ipc_last_rsp;

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
  UNLOCK_HSEM(HSEM_IPC_ID);

  IPC_WriteResponse(0U, IPC_CMD_NONE, IPC_CMD_RES_OK, 0U);
  IPC_PublishStatus();
}

void IPC_CmdService(void)
{
  IpcCommandBlock_t cmd_local;
  DaqConfig_t next_cfg;

  IPC_PublishStatus();

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

    default:
      IPC_WriteResponse(cmd_local.seq, cmd_local.cmd, IPC_CMD_RES_INVALID, 6U);
      break;
  }
}

const IpcResponseBlock_t *IPC_CmdGetLastResponse(void)
{
  return &ipc_last_rsp;
}
