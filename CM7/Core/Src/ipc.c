#include "ipc.h"

#include <string.h>
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "main.h"
#include "shared_memory.h"

static uint32_t IPC_PostCommand(const IpcCommandBlock_t *cmd)
{
  uint32_t accepted = 0U;

  LOCK_HSEM(HSEM_IPC_ID);
  if (SHARED_IPC_REGION->cmd.pending == 0U)
  {
    SHARED_IPC_REGION->rsp.ready = 0U;
    memcpy((void *)&SHARED_IPC_REGION->cmd, cmd, sizeof(*cmd));
    SHARED_IPC_REGION->cmd.pending = 1U;
    accepted = 1U;
  }
  UNLOCK_HSEM(HSEM_IPC_ID);

  return accepted;
}

void IPC_InitSharedRegion(void)
{
  LOCK_HSEM(HSEM_IPC_ID);
  if (SHARED_IPC_REGION->cmd.magic != IPC_SHARED_MAGIC)
  {
    memset((void *)SHARED_IPC_REGION, 0, sizeof(*SHARED_IPC_REGION));
    SHARED_IPC_REGION->cmd.magic = IPC_SHARED_MAGIC;
    SHARED_IPC_REGION->cmd.version = IPC_SHARED_VERSION;
    SHARED_IPC_REGION->rsp.magic = IPC_SHARED_MAGIC;
    SHARED_IPC_REGION->rsp.version = IPC_SHARED_VERSION;
    SHARED_IPC_REGION->status.magic = IPC_SHARED_MAGIC;
    SHARED_IPC_REGION->status.version = IPC_SHARED_VERSION;
  }
  UNLOCK_HSEM(HSEM_IPC_ID);
}

uint32_t IPC_PostStartAcq(uint32_t seq, uint32_t sample_rate_hz, uint32_t channel_mask, uint32_t block_samples, const char *filename)
{
  IpcCommandBlock_t cmd = {0};

  cmd.magic = IPC_SHARED_MAGIC;
  cmd.version = IPC_SHARED_VERSION;
  cmd.pending = 0U;
  cmd.seq = seq;
  cmd.cmd = IPC_CMD_START_ACQ;
  cmd.payload_size = sizeof(IpcStartAcqParams_t);
  cmd.payload.start_acq.sample_rate_hz = sample_rate_hz;
  cmd.payload.start_acq.channel_mask = channel_mask;
  cmd.payload.start_acq.block_samples = block_samples;
  cmd.payload.start_acq.flags = 0U;
  if (filename != NULL)
  {
    strncpy(cmd.payload.start_acq.filename, filename, IPC_FILENAME_LEN - 1U);
  }

  return IPC_PostCommand(&cmd);
}

uint32_t IPC_PostSimpleCommand(uint32_t seq, uint32_t cmd_id)
{
  IpcCommandBlock_t cmd = {0};

  cmd.magic = IPC_SHARED_MAGIC;
  cmd.version = IPC_SHARED_VERSION;
  cmd.pending = 0U;
  cmd.seq = seq;
  cmd.cmd = cmd_id;
  cmd.payload_size = 0U;

  return IPC_PostCommand(&cmd);
}

uint32_t IPC_WaitForResponse(uint32_t seq, IpcResponseBlock_t *rsp, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  IpcResponseBlock_t local_rsp;

  do
  {
    LOCK_HSEM(HSEM_IPC_ID);
    memcpy(&local_rsp, (const void *)&SHARED_IPC_REGION->rsp, sizeof(local_rsp));
    UNLOCK_HSEM(HSEM_IPC_ID);

    if ((local_rsp.ready != 0U) && (local_rsp.seq == seq))
    {
      if (rsp != NULL)
      {
        *rsp = local_rsp;
      }
      return 1U;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);

  return 0U;
}

void IPC_ReadStatus(SharedStatusBlock_t *status)
{
  if (status == NULL)
  {
    return;
  }

  LOCK_HSEM(HSEM_IPC_ID);
  memcpy(status, (const void *)&SHARED_IPC_REGION->status, sizeof(*status));
  UNLOCK_HSEM(HSEM_IPC_ID);
}
