#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define IPC_SHARED_MAGIC            (0x41454D53UL)
#define IPC_SHARED_VERSION          (2UL)
#define IPC_FILENAME_LEN            (64U)
#define IPC_COMMAND_PAYLOAD_SIZE    (96U)
#define IPC_CHUNK_BUFFER_SIZE       (4096U)

typedef enum
{
  IPC_CMD_NONE = 0U,
  IPC_CMD_PING,
  IPC_CMD_GET_STATUS,
  IPC_CMD_START_ACQ,
  IPC_CMD_STOP_ACQ
} IpcCmdType_t;

typedef enum
{
  IPC_CMD_RES_OK = 0U,
  IPC_CMD_RES_REJECTED_STATE,
  IPC_CMD_RES_BUSY,
  IPC_CMD_RES_INVALID
} IpcCmdResult_t;

typedef struct
{
  uint32_t sample_rate_hz;
  uint32_t channel_mask;
  uint32_t block_samples;
  uint32_t flags;
  char filename[IPC_FILENAME_LEN];
} IpcStartAcqParams_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  volatile uint32_t pending;
  volatile uint32_t seq;
  volatile uint32_t cmd;
  volatile uint32_t payload_size;
  union
  {
    IpcStartAcqParams_t start_acq;
    uint8_t raw[IPC_COMMAND_PAYLOAD_SIZE];
  } payload;
} IpcCommandBlock_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  volatile uint32_t ready;
  volatile uint32_t seq;
  volatile uint32_t cmd;
  volatile uint32_t result;
  volatile uint32_t error;
} IpcResponseBlock_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  volatile uint32_t daq_state;
  volatile uint32_t last_error;
  volatile uint32_t sample_rate_hz;
  volatile uint32_t channel_mask;
  volatile uint32_t block_samples;
  volatile uint32_t samples_captured;
  volatile uint32_t dropped_buffers;
  volatile uint64_t bytes_queued;
  volatile uint64_t bytes_written;
  volatile uint32_t fs_ready;
  volatile uint32_t emmc_busy;
  char active_filename[IPC_FILENAME_LEN];
} SharedStatusBlock_t;

typedef struct
{
  IpcCommandBlock_t cmd;
  IpcResponseBlock_t rsp;
  SharedStatusBlock_t status;
  uint8_t chunk_buffer[IPC_CHUNK_BUFFER_SIZE];
} SharedIpcRegion_t;

#ifdef __cplusplus
}
#endif

#endif /* IPC_SHARED_H */
