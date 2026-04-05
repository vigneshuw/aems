/*
 * ipc.h
 *
 * CM7 shared-memory IPC interface to CM4.
 */

#ifndef INC_IPC_H_
#define INC_IPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ipc_shared.h"

/**
  * @brief Initialize shared IPC region metadata on CM7 side.
  * @param None
  * @return None
  */
void IPC_InitSharedRegion(void);

/**
  * @brief Post a START_ACQ command to CM4.
  * @param seq Command sequence number.
  * @param sample_rate_hz Requested sample rate.
  * @param channel_mask Enabled ADS131M08 channels.
  * @param block_samples Requested block size for aggregation/write pipeline.
  * @param filename Output filename for DAQ data.
  * @return uint32_t Returns 1 if command is posted, 0 if mailbox is busy.
  */
uint32_t IPC_PostStartAcq(uint32_t seq, uint32_t sample_rate_hz, uint32_t channel_mask, uint32_t block_samples, const char *filename);

/**
  * @brief Post a command without payload to CM4.
  * @param seq Command sequence number.
  * @param cmd_id Command ID from @ref IpcCmdType_t.
  * @return uint32_t Returns 1 if command is posted, 0 if mailbox is busy.
  */
uint32_t IPC_PostSimpleCommand(uint32_t seq, uint32_t cmd_id);
uint32_t IPC_PostStreamFile(uint32_t seq, const char *filename);

/**
  * @brief Wait for a matching response from CM4.
  * @param seq Expected response sequence number.
  * @param rsp Pointer to output response buffer.
  * @param timeout_ms Timeout in milliseconds.
  * @return uint32_t Returns 1 if response is received, 0 on timeout.
  */
uint32_t IPC_WaitForResponse(uint32_t seq, IpcResponseBlock_t *rsp, uint32_t timeout_ms);

/**
  * @brief Read the latest CM4 shared status snapshot.
  * @param status Pointer to output status buffer.
  * @return None
  */
void IPC_ReadStatus(SharedStatusBlock_t *status);
uint32_t IPC_ReadStreamInfo(uint32_t seq, IpcStreamBlock_t *stream);
uint32_t IPC_StreamFetchChunk(uint32_t seq, uint8_t *dst, uint16_t max_len, uint16_t *out_len, uint32_t *out_state, uint32_t *out_error);

#ifdef __cplusplus
}
#endif

#endif /* INC_IPC_H_ */
