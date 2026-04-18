#ifndef DAQ_SHARED_H
#define DAQ_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAQ_FILENAME_LEN          64U
#define DAQ_CHANNEL_COUNT         8U
#define DAQ_SAMPLE_FRAME_SIZE     36U
#define DAQ_DEFAULT_SAMPLE_RATE_HZ 4000U
#define DAQ_DEFAULT_CHANNEL_MASK  0xFFU
#define DAQ_DEFAULT_BLOCK_SAMPLES 64U

typedef enum
{
  DAQ_MODE_IDLE = 0U,
  DAQ_MODE_LOG_TO_EMMC = 1U,
  DAQ_MODE_STREAM_TO_SHMEM = 2U
} DaqMode_t;

typedef struct
{
  uint16_t response;
  uint16_t crc;
  int32_t channel[DAQ_CHANNEL_COUNT];
} DaqSampleFrame_t;

typedef struct
{
  uint32_t sample_rate_hz;
  uint32_t channel_mask;
  uint32_t block_samples;
  uint32_t flags;
  char filename[DAQ_FILENAME_LEN];
} DaqConfig_t;

typedef struct
{
  uint32_t state;
  uint32_t mode;
  uint32_t last_error;
  uint32_t samples_captured;
  uint32_t dropped_buffers;
  uint64_t bytes_queued;
  uint64_t bytes_written;
  uint32_t adc_ready_pending;
} DaqStatus_t;

#ifdef __cplusplus
}
#endif

#endif
