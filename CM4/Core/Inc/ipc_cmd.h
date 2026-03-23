/*
 * ipc_cmd.h
 *
 * CM4 command ingress adapter (shared RAM + HSEM -> CM4 DAQ events).
 */

#ifndef INC_IPC_CMD_H_
#define INC_IPC_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ipc_shared.h"

/**
  * @brief Initialize IPC command adapter state and shared IPC metadata.
  * @param None
  * @return None
  */
void IPC_CmdInit(void);

/**
  * @brief Service one pending command from shared memory and map it to CM4 DAQ events.
  * @param None
  * @return None
  */
void IPC_CmdService(void);

/**
  * @brief Get the last processed IPC response snapshot on CM4.
  * @param None
  * @return const IpcResponseBlock_t* Pointer to the latest local response snapshot.
  */
const IpcResponseBlock_t *IPC_CmdGetLastResponse(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_IPC_CMD_H_ */
