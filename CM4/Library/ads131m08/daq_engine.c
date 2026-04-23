#include "daq_engine.h"

#include <string.h>
#include "main.h"
#include "ads131m08.h"
#include "emmc_fs.h"
#include "file_shmem.h"
#include "statemachine.h"

static int32_t calibration_values[DAQ_OFFSET_CAL_CHANNELS] = {0, 0, 0, 0, 0, 0};
static DaqSampleFrame_t last_sample;
static DaqConfig_t g_daq_cfg;
static DaqMode_t g_daq_mode = DAQ_MODE_IDLE;
static int32_t g_daq_last_op_status = 0;
static uint32_t g_calibration_samples_averaged = 0U;
static int32_t g_calibration_storage_status = 0;
static uint8_t g_offset_calibration_active = 0U;
static uint32_t g_offset_calibration_ignore_until = 0U;
static uint32_t g_offset_calibration_average_until = 0U;
static uint32_t g_offset_calibration_count = 0U;
static int64_t g_offset_calibration_sum[DAQ_OFFSET_CAL_CHANNELS];

#define DAQ_AGGR_SAMPLES_PER_BLOCK   (128U)
#define DAQ_WRITE_QUEUE_DEPTH        (8U)
#define DAQ_ERROR_WRITE_OPEN         (10U)
#define DAQ_ERROR_WRITE_DATA         (11U)
#define DAQ_OFFSET_CAL_FILENAME      "ocal.cfg"
#define DAQ_OFFSET_CAL_MAGIC         0x314C4143UL
#define DAQ_OFFSET_CAL_VERSION       1U
#define DAQ_OFFSET_CAL_IGNORE_MS     5000U
#define DAQ_OFFSET_CAL_AVERAGE_MS    10000U
/* Set to 0 only for ADC pipeline testing without storage latency. */
#define DAQ_EMMC_WRITE_ENABLE        (1U)

typedef struct
{
  uint16_t sample_count;
  DaqSampleFrame_t sample[DAQ_AGGR_SAMPLES_PER_BLOCK];
} DaqWriteBlock_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t channel_count;
  int32_t offset[DAQ_OFFSET_CAL_CHANNELS];
  uint32_t checksum;
} DaqOffsetCalibrationFile_t;

static DaqWriteBlock_t aggr_block;
static DaqWriteBlock_t write_queue[DAQ_WRITE_QUEUE_DEPTH];
static uint8_t emmc_pack_buffer[DAQ_AGGR_SAMPLES_PER_BLOCK * DAQ_CHANNEL_COUNT * sizeof(int32_t)] __attribute__((aligned(4)));
static uint8_t write_q_head = 0U;
static uint8_t write_q_tail = 0U;
static uint8_t write_q_count = 0U;

static uint8_t DAQ_StoreAdcFrame(const adc_channel_data *raw);
static void DAQ_BuildSampleFrame(const adc_channel_data *raw, DaqSampleFrame_t *frame);
static uint8_t DAQ_StoreSampleFrame(const DaqSampleFrame_t *frame);
static void DAQ_TryStartAdcDmaFromIsr(void);
static void DAQ_MaskDrdyIrqForStorage(void);
static void DAQ_UnmaskDrdyIrqAfterStorage(void);
static uint8_t DAQ_CountSelectedChannels(void);
static uint32_t DAQ_GetQueuedBytes(uint16_t sample_count);
static uint32_t DAQ_PackBlockSelectedChannels(const DaqWriteBlock_t *blk,
                                              uint8_t *buffer,
                                              uint32_t buffer_size);
static void DAQ_WriteS32Le(uint8_t *data, int32_t value);
static uint32_t DAQ_CalibrationChecksum(const DaqOffsetCalibrationFile_t *record);
static void DAQ_ApplyZeroOffsetCalibration(void);
static void DAQ_StartOffsetCalibrationAccumulator(void);
static uint8_t DAQ_AccumulateOffsetCalibrationFrame(const adc_channel_data *raw);

static uint16_t DAQ_OsrForSampleRate(uint32_t sample_rate_hz)
{
  /*
   * ADS data rate is MCLK/(2*OSR). With the current 8 MHz MCO clock, these
   * settings are approximately 31.25k, 15.625k, 7.812k, 3.906k, 1.953k,
   * 976, 488, and 244 SPS.
   */
  if (sample_rate_hz >= 24000U)
  {
    return CLOCK_OSR_128;
  }
  if (sample_rate_hz >= 12000U)
  {
    return CLOCK_OSR_256;
  }
  if (sample_rate_hz >= 6000U)
  {
    return CLOCK_OSR_512;
  }
  if (sample_rate_hz >= 3000U)
  {
    return CLOCK_OSR_1024;
  }
  if (sample_rate_hz >= 1500U)
  {
    return CLOCK_OSR_2048;
  }
  if (sample_rate_hz >= 750U)
  {
    return CLOCK_OSR_4096;
  }
  if (sample_rate_hz >= 375U)
  {
    return CLOCK_OSR_8192;
  }
  return CLOCK_OSR_16384;
}

static void DAQ_ResetSoftwareBuffers(void)
{
  aggr_block.sample_count = 0U;
  write_q_head = 0U;
  write_q_tail = 0U;
  write_q_count = 0U;
  g_daq_ctx.bytes_queued = 0U;
}

static uint8_t DAQ_ValidateConfig(const DaqConfig_t *cfg)
{
  if (cfg == NULL)
  {
    return 0U;
  }

  if (cfg->sample_rate_hz == 0U)
  {
    return 0U;
  }

  if ((cfg->channel_mask == 0U) || ((cfg->channel_mask & ~0xFFU) != 0U))
  {
    return 0U;
  }

  if ((cfg->block_samples == 0U) || (cfg->block_samples > DAQ_AGGR_SAMPLES_PER_BLOCK))
  {
    return 0U;
  }

  if (cfg->filename[DAQ_FILENAME_LEN - 1U] != '\0')
  {
    return 0U;
  }

  return 1U;
}


static uint8_t DAQ_QueuePush(const DaqWriteBlock_t *blk)
{
  uint32_t queued_bytes;

  /* Fixed-depth ring queue to decouple sampling from storage latency. */
  if (write_q_count >= DAQ_WRITE_QUEUE_DEPTH)
  {
    return 0U;
  }

  write_queue[write_q_head] = *blk;
  write_q_head = (uint8_t)((write_q_head + 1U) % DAQ_WRITE_QUEUE_DEPTH);
  write_q_count++;
  queued_bytes = DAQ_GetQueuedBytes(blk->sample_count);
  g_daq_ctx.bytes_queued += (uint64_t)queued_bytes;
  return 1U;
}


static uint8_t DAQ_QueuePop(DaqWriteBlock_t *blk)
{
  if (write_q_count == 0U)
  {
    return 0U;
  }

  *blk = write_queue[write_q_tail];
  write_q_tail = (uint8_t)((write_q_tail + 1U) % DAQ_WRITE_QUEUE_DEPTH);
  write_q_count--;
  return 1U;
}

void DAQ_Shutdown(void)
{
  ADS131M08_AbortReadDma();
  (void)DAQ_ServiceCapturedSamples(DAQ_AGGR_SAMPLES_PER_BLOCK);

  /* Flush any partial aggregation so finalization can commit all captured data. */
  if (aggr_block.sample_count > 0U)
  {
    if (DAQ_QueuePush(&aggr_block) == 0U)
    {
      g_daq_ctx.dropped_buffers++;
    }
    aggr_block.sample_count = 0U;
  }

  /* Stop ADC clocking */
  adcMaster_Shutdown();

  /* Update DAQ context */
  g_daq_ctx.is_adc_armed = 0U;

  /* Keep the writer open until FINALIZING drains queued blocks. */
}

void DAQ_Startup(void)
{
  uint16_t clock_config;

  /* Initialize ADC */
  adcMaster_Startup();
  adcStartup();

  /* Configure ADS clocking and PGA gains for the active DAQ profile. */
  clock_config = (CLOCK_DEFAULT & ~(CLOCK_EXTREF_EN_MASK | CLOCK_OSR_MASK)) |
                 CLOCK_EXTREF_EN_ENABLED |
                 DAQ_OsrForSampleRate(g_daq_cfg.sample_rate_hz);
  writeSingleRegister(CLOCK_ADDRESS, clock_config);
  writeSingleRegister(GAIN1_ADDRESS, ((GAIN1_DEFAULT & ~(GAIN1_PGAGAIN0_MASK | GAIN1_PGAGAIN1_MASK | GAIN1_PGAGAIN2_MASK))) | (GAIN1_PGAGAIN0_4 | GAIN1_PGAGAIN1_4 | GAIN1_PGAGAIN2_4));

  /* Apply calibration values to 6 channels */
  for (uint8_t channel = 0; channel < 6; channel++)
  {
    calibrate(calibration_values[channel], channel);
  }

  g_daq_ctx.is_adc_armed = 1U;
  aggr_block.sample_count = 0U;
}

void DAQ_EngineInit(void)
{
  /* Reset software pipeline state and force ADC clock output to idle-low. */
  g_daq_ctx.is_adc_armed = 0U;
  DAQ_ResetSoftwareBuffers();
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_cfg.sample_rate_hz = DAQ_DEFAULT_SAMPLE_RATE_HZ;
  g_daq_cfg.channel_mask = 0xFFU;
  g_daq_cfg.block_samples = DAQ_AGGR_SAMPLES_PER_BLOCK;
  g_daq_cfg.flags = 0U;
  (void)strcpy(g_daq_cfg.filename, "daq.bin");
  g_daq_mode = DAQ_MODE_IDLE;
  (void)EmmcFs_CloseRawLog();
  adcMaster_Shutdown();
}

uint8_t DAQ_ApplyConfig(const DaqConfig_t *cfg)
{
  if (DAQ_ValidateConfig(cfg) == 0U)
  {
    return 0U;
  }

  g_daq_cfg = *cfg;
  return 1U;
}

const DaqConfig_t *DAQ_GetConfig(void)
{
  return &g_daq_cfg;
}

uint8_t DAQ_StartLogging(const DaqConfig_t *cfg)
{
  EmmcFsStatus_t fs_status;

  if (DAQ_ApplyConfig(cfg) == 0U)
  {
    g_daq_last_op_status = (int32_t)EMMC_FS_ERR_PARAM;
    return 0U;
  }

  (void)EmmcFs_CloseRawLog();
  fs_status = EmmcFs_OpenRawLog(g_daq_cfg.filename);
  if (fs_status != EMMC_FS_OK)
  {
    g_daq_ctx.last_error = DAQ_ERROR_WRITE_OPEN;
    g_daq_last_op_status = (int32_t)fs_status;
    return 0U;
  }

  g_daq_mode = DAQ_MODE_LOG_TO_EMMC;
  DAQ_ResetSoftwareBuffers();
  g_daq_ctx.samples_captured = 0U;
  g_daq_ctx.dropped_buffers = 0U;
  g_daq_ctx.bytes_written = 0U;
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_ctx.last_error = 0U;
  g_daq_ctx.events = DAQ_EVT_CMD_START;
  g_daq_last_op_status = 0;
  return 1U;
}

int32_t DAQ_LoadOffsetCalibration(void)
{
  EmmcFsReadHandle_t handle;
  DaqOffsetCalibrationFile_t record;
  uint32_t total_size = 0U;
  uint16_t bytes_read = 0U;
  EmmcFsStatus_t fs_status;

  memset(&handle, 0, sizeof(handle));
  memset(&record, 0, sizeof(record));

  fs_status = EmmcFs_OpenFileRead(DAQ_OFFSET_CAL_FILENAME, &handle, &total_size);
  if (fs_status != EMMC_FS_OK)
  {
    /* First boot/no calibration file is valid: keep the firmware-default zero offsets. */
    g_calibration_storage_status = (int32_t)fs_status;
    return (int32_t)fs_status;
  }

  fs_status = EmmcFs_ReadFileNext(&handle,
                                  (uint8_t *)&record,
                                  (uint16_t)sizeof(record),
                                  &bytes_read);
  (void)EmmcFs_CloseFileRead(&handle);

  if ((fs_status != EMMC_FS_OK) ||
      (bytes_read != sizeof(record)) ||
      (total_size != sizeof(record)) ||
      (record.magic != DAQ_OFFSET_CAL_MAGIC) ||
      (record.version != DAQ_OFFSET_CAL_VERSION) ||
      (record.channel_count != DAQ_OFFSET_CAL_CHANNELS) ||
      (record.checksum != DAQ_CalibrationChecksum(&record)))
  {
    g_calibration_storage_status = (int32_t)EMMC_FS_ERR_READ_FILE;
    return (int32_t)EMMC_FS_ERR_READ_FILE;
  }

  memcpy(calibration_values, record.offset, sizeof(calibration_values));
  g_calibration_samples_averaged = 0U;
  g_calibration_storage_status = EMMC_FS_OK;
  return EMMC_FS_OK;
}

int32_t DAQ_SaveOffsetCalibration(void)
{
  EmmcFsWriteHandle_t handle;
  DaqOffsetCalibrationFile_t record;
  uint32_t bytes_written = 0U;
  EmmcFsStatus_t fs_status;

  memset(&handle, 0, sizeof(handle));
  memset(&record, 0, sizeof(record));

  record.magic = DAQ_OFFSET_CAL_MAGIC;
  record.version = DAQ_OFFSET_CAL_VERSION;
  record.channel_count = DAQ_OFFSET_CAL_CHANNELS;
  memcpy(record.offset, calibration_values, sizeof(calibration_values));
  record.checksum = DAQ_CalibrationChecksum(&record);

  fs_status = EmmcFs_OpenFileWrite(DAQ_OFFSET_CAL_FILENAME, &handle);
  if (fs_status == EMMC_FS_OK)
  {
    fs_status = EmmcFs_WriteFileNext(&handle,
                                     (const uint8_t *)&record,
                                     (uint32_t)sizeof(record),
                                     &bytes_written);
  }

  if ((fs_status == EMMC_FS_OK) && (bytes_written != sizeof(record)))
  {
    fs_status = EMMC_FS_ERR_READ_FILE;
  }

  if (handle.is_open != 0U)
  {
    EmmcFsStatus_t close_status = EmmcFs_CloseFileWrite(&handle);
    if (fs_status == EMMC_FS_OK)
    {
      fs_status = close_status;
    }
  }

  g_calibration_storage_status = (int32_t)fs_status;
  return (int32_t)fs_status;
}

void DAQ_GetOffsetCalibration(DaqCalibration_t *calibration)
{
  if (calibration == NULL)
  {
    return;
  }

  memset(calibration, 0, sizeof(*calibration));
  memcpy(calibration->offset, calibration_values, sizeof(calibration_values));
  calibration->samples_averaged = g_calibration_samples_averaged;
  calibration->storage_status = g_calibration_storage_status;
}

int32_t DAQ_RunOffsetCalibration(DaqCalibration_t *calibration)
{
  uint32_t guard_end_tick;
  DaqConfig_t saved_cfg = g_daq_cfg;
  DaqMode_t saved_mode = g_daq_mode;
  DaqState_t saved_state = g_daq_ctx.state;
  int32_t storage_status;

  if (calibration != NULL)
  {
    memset(calibration, 0, sizeof(*calibration));
  }

  if (g_daq_ctx.state != DAQ_STATE_IDLE)
  {
    return (int32_t)EMMC_FS_ERR_PARAM;
  }

  ADS131M08_AbortReadDma();
  DAQ_ResetSoftwareBuffers();
  g_daq_ctx.samples_captured = 0U;
  g_daq_ctx.dropped_buffers = 0U;
  g_daq_ctx.bytes_written = 0U;
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_ctx.last_error = 0U;
  g_daq_mode = DAQ_MODE_IDLE;

  g_daq_cfg.sample_rate_hz = 2000U;
  g_daq_cfg.channel_mask = 0x3FU;
  g_daq_cfg.block_samples = DAQ_AGGR_SAMPLES_PER_BLOCK;
  g_daq_cfg.flags = 0U;

  adcMaster_Startup();
  adcStartup();
  writeSingleRegister(CLOCK_ADDRESS,
                      (CLOCK_DEFAULT & ~(CLOCK_EXTREF_EN_MASK | CLOCK_OSR_MASK)) |
                      CLOCK_EXTREF_EN_ENABLED |
                      DAQ_OsrForSampleRate(g_daq_cfg.sample_rate_hz));
  writeSingleRegister(GAIN1_ADDRESS,
                      ((GAIN1_DEFAULT & ~(GAIN1_PGAGAIN0_MASK |
                                          GAIN1_PGAGAIN1_MASK |
                                          GAIN1_PGAGAIN2_MASK))) |
                      (GAIN1_PGAGAIN0_4 | GAIN1_PGAGAIN1_4 | GAIN1_PGAGAIN2_4));
  DAQ_ApplyZeroOffsetCalibration();

  DAQ_StartOffsetCalibrationAccumulator();
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);
  HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
  g_daq_ctx.state = DAQ_STATE_ACQUIRING;
  g_daq_ctx.is_adc_armed = 1U;

  guard_end_tick = HAL_GetTick() +
                   DAQ_OFFSET_CAL_IGNORE_MS +
                   DAQ_OFFSET_CAL_AVERAGE_MS +
                   100U;
  while ((int32_t)(HAL_GetTick() - guard_end_tick) < 0)
  {
    (void)DAQ_ServiceAdcPending(16U);
    (void)DAQ_ServiceCapturedSamples(64U);
    if (g_offset_calibration_active == 0U)
    {
      break;
    }
  }

  g_daq_ctx.is_adc_armed = 0U;
  ADS131M08_AbortReadDma();
  (void)DAQ_ServiceCapturedSamples(DAQ_AGGR_SAMPLES_PER_BLOCK);
  adcMaster_Shutdown();
  g_offset_calibration_active = 0U;

  if (g_offset_calibration_count == 0U)
  {
    g_daq_cfg = saved_cfg;
    g_daq_mode = saved_mode;
    g_daq_ctx.state = saved_state;
    g_daq_ctx.is_adc_armed = 0U;
    g_daq_ctx.adc_ready_pending = 0U;
    return (int32_t)EMMC_FS_ERR_READ_FILE;
  }

  for (uint8_t channel = 0U; channel < DAQ_OFFSET_CAL_CHANNELS; channel++)
  {
    calibration_values[channel] = (int32_t)(g_offset_calibration_sum[channel] /
                                            (int64_t)g_offset_calibration_count);
  }
  g_calibration_samples_averaged = g_offset_calibration_count;
  storage_status = DAQ_SaveOffsetCalibration();

  g_daq_cfg = saved_cfg;
  g_daq_mode = saved_mode;
  g_daq_ctx.state = DAQ_STATE_IDLE;
  g_daq_ctx.is_adc_armed = 0U;
  g_daq_ctx.events = 0U;
  g_daq_ctx.adc_ready_pending = 0U;

  DAQ_GetOffsetCalibration(calibration);
  return storage_status;
}

int32_t DAQ_GetLastOpStatus(void)
{
  return g_daq_last_op_status;
}

uint8_t DAQ_StartStreaming(const DaqConfig_t *cfg)
{
  if (DAQ_ApplyConfig(cfg) == 0U)
  {
    g_daq_last_op_status = (int32_t)EMMC_FS_ERR_PARAM;
    return 0U;
  }

  (void)EmmcFs_CloseRawLog();

  g_daq_mode = DAQ_MODE_STREAM_TO_SHMEM;
  DAQ_ResetSoftwareBuffers();
  g_daq_ctx.samples_captured = 0U;
  g_daq_ctx.dropped_buffers = 0U;
  g_daq_ctx.bytes_written = 0U;
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_ctx.last_error = 0U;
  g_daq_ctx.events = DAQ_EVT_CMD_START;
  g_daq_last_op_status = 0;
  return 1U;
}

void DAQ_Stop(void)
{
  if ((g_daq_ctx.state == DAQ_STATE_IDLE) || (g_daq_ctx.state == DAQ_STATE_ERROR))
  {
    DAQ_ResetSoftwareBuffers();
    g_daq_ctx.events = 0U;
    g_daq_ctx.is_adc_armed = 0U;
    g_daq_mode = DAQ_MODE_IDLE;
    return;
  }

  g_daq_ctx.events = DAQ_EVT_CMD_STOP;
}

uint8_t DAQ_StopAndClose(void)
{
  uint32_t guard = 0U;

  if ((g_daq_ctx.state != DAQ_STATE_IDLE) && (g_daq_ctx.state != DAQ_STATE_ERROR))
  {
    DAQ_Shutdown();
  }

  while ((write_q_count != 0U) && (guard < (DAQ_WRITE_QUEUE_DEPTH + 2U)))
  {
    DAQ_ServicePendingWrites();
    guard++;
  }

  if (aggr_block.sample_count > 0U)
  {
    if (DAQ_QueuePush(&aggr_block) == 0U)
    {
      g_daq_ctx.dropped_buffers++;
    }
    aggr_block.sample_count = 0U;
  }

  guard = 0U;
  while ((write_q_count != 0U) && (guard < (DAQ_WRITE_QUEUE_DEPTH + 2U)))
  {
    DAQ_ServicePendingWrites();
    guard++;
  }

  if (EmmcFs_IsRawLogOpen() != 0U)
  {
    DAQ_MaskDrdyIrqForStorage();
    if (EmmcFs_CloseRawLog() != EMMC_FS_OK)
    {
      g_daq_ctx.last_error = DAQ_ERROR_WRITE_DATA;
      g_daq_last_op_status = (int32_t)EMMC_FS_ERR_READ_FILE;
    }
    DAQ_UnmaskDrdyIrqAfterStorage();
  }

  adcMaster_Shutdown();
  g_daq_ctx.events = 0U;
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_ctx.is_adc_armed = 0U;
  g_daq_ctx.state = DAQ_STATE_IDLE;
  g_daq_mode = DAQ_MODE_IDLE;
  DAQ_ResetSoftwareBuffers();

  g_daq_last_op_status = (g_daq_ctx.last_error == 0U) ? 0 : (int32_t)EMMC_FS_ERR_READ_FILE;
  return (g_daq_ctx.last_error == 0U) ? 1U : 0U;
}

void DAQ_GetStatus(DaqStatus_t *status)
{
  if (status == NULL)
  {
    return;
  }

  memset(status, 0, sizeof(*status));
  status->state = (uint32_t)g_daq_ctx.state;
  status->mode = (uint32_t)g_daq_mode;
  status->last_error = g_daq_ctx.last_error;
  status->samples_captured = g_daq_ctx.samples_captured;
  status->dropped_buffers = g_daq_ctx.dropped_buffers;
  status->bytes_queued = g_daq_ctx.bytes_queued;
  status->bytes_written = g_daq_ctx.bytes_written;
  status->adc_ready_pending = g_daq_ctx.adc_ready_pending;
}

uint8_t DAQ_ReadStreamBlockShared(uint8_t **buffer,
                                  uint16_t max_len,
                                  uint16_t *bytes_read,
                                  uint32_t *samples_read)
{
  DaqWriteBlock_t blk;
  uint32_t block_bytes;

  if ((buffer == NULL) || (bytes_read == NULL) || (samples_read == NULL) ||
      (max_len < sizeof(DaqSampleFrame_t)))
  {
    return 0U;
  }

  *buffer = FILE_SHMEM_DATA_PTR;
  *bytes_read = 0U;
  *samples_read = 0U;

  if (g_daq_mode != DAQ_MODE_STREAM_TO_SHMEM)
  {
    return 0U;
  }

  if (DAQ_QueuePop(&blk) == 0U)
  {
    return 0U;
  }

  block_bytes = (uint32_t)(blk.sample_count * sizeof(DaqSampleFrame_t));
  if ((block_bytes > max_len) || (block_bytes > FILE_SHMEM_DATA_LEN))
  {
    g_daq_ctx.dropped_buffers++;
    return 0U;
  }

  memcpy(FILE_SHMEM_DATA_PTR, blk.sample, block_bytes);
  if (g_daq_ctx.bytes_queued >= block_bytes)
  {
    g_daq_ctx.bytes_queued -= block_bytes;
  }
  else
  {
    g_daq_ctx.bytes_queued = 0U;
  }
  g_daq_ctx.bytes_written += block_bytes;

  *bytes_read = (uint16_t)block_bytes;
  *samples_read = blk.sample_count;
  return 1U;
}

void adcMaster_Startup(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_8);

  /* Route MCO clock to PA8 so ADS has its external master clock. */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = ADS_STM32_CLKOUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(ADS_STM32_CLKOUT_GPIO_Port, &GPIO_InitStruct);
}

void adcMaster_Shutdown(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* Reclaim PA8 as GPIO low to stop clock toggling seen by ADS. */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = ADS_STM32_CLKOUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ADS_STM32_CLKOUT_GPIO_Port, &GPIO_InitStruct);
  HAL_GPIO_WritePin(ADS_STM32_CLKOUT_GPIO_Port, ADS_STM32_CLKOUT_Pin, GPIO_PIN_RESET);

  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSI, RCC_MCODIV_15);
}

uint8_t DAQ_ProcessAdcReadyEvent(void)
{
  adc_channel_data raw;

  if ((g_daq_ctx.is_adc_armed == 0U) || (g_daq_ctx.state != DAQ_STATE_ACQUIRING))
  {
    return 0U;
  }

  readAllChannelData(&raw);
  return DAQ_StoreAdcFrame(&raw);
}

static uint8_t DAQ_StoreAdcFrame(const adc_channel_data *raw)
{
  DaqSampleFrame_t frame;

  if (raw == NULL)
  {
    return 0U;
  }

  /* Fast guard against obvious invalid SPI frames. */
  if (raw->response == 0xFFFFU)
  {
    g_daq_ctx.dropped_buffers++;
    return 1U;
  }

  if (g_offset_calibration_active != 0U)
  {
    return DAQ_AccumulateOffsetCalibrationFrame(raw);
  }

  DAQ_BuildSampleFrame(raw, &frame);
  return DAQ_StoreSampleFrame(&frame);
}

static void DAQ_BuildSampleFrame(const adc_channel_data *raw, DaqSampleFrame_t *frame)
{
  frame->response = raw->response;
  frame->crc = raw->crc;
  frame->channel[0] = raw->channel0;
  frame->channel[1] = raw->channel1;
  frame->channel[2] = raw->channel2;
  frame->channel[3] = raw->channel3;
  frame->channel[4] = raw->channel4;
  frame->channel[5] = raw->channel5;
  frame->channel[6] = raw->channel6;
  frame->channel[7] = raw->channel7;
}

static uint8_t DAQ_StoreSampleFrame(const DaqSampleFrame_t *frame)
{
  if (frame == NULL)
  {
    return 0U;
  }

  last_sample = *frame;

  /* L2 buffering: append into aggregation block before queueing to storage path. */
  aggr_block.sample[aggr_block.sample_count++] = last_sample;

  if (aggr_block.sample_count >= g_daq_cfg.block_samples)
  {
    if (DAQ_QueuePush(&aggr_block) == 0U)
    {
      g_daq_ctx.dropped_buffers++;
      aggr_block.sample_count = 0U;
      return 1U;
    }
    aggr_block.sample_count = 0U;
  }

  g_daq_ctx.samples_captured++;

  return 1U;
}

void DAQ_OnAdcDrdyFromIsr(void)
{
  if ((g_daq_ctx.is_adc_armed == 0U) || (g_daq_ctx.state != DAQ_STATE_ACQUIRING))
  {
    return;
  }

  g_daq_ctx.adc_ready_pending++;
  DAQ_TryStartAdcDmaFromIsr();
}

void DAQ_OnAdcDmaCompleteFromIsr(void)
{
  /* DMA layer has queued the raw frame; main loop parses/stores/writes it. */
  DAQ_TryStartAdcDmaFromIsr();
}

void DAQ_OnAdcDmaErrorFromIsr(void)
{
  g_daq_ctx.dropped_buffers++;
  DAQ_TryStartAdcDmaFromIsr();
}

static void DAQ_TryStartAdcDmaFromIsr(void)
{
  if ((g_daq_ctx.is_adc_armed == 0U) || (g_daq_ctx.state != DAQ_STATE_ACQUIRING) ||
      (g_daq_ctx.adc_ready_pending == 0U) || (ADS131M08_IsReadDmaBusy() != 0U))
  {
    return;
  }

  g_daq_ctx.adc_ready_pending--;
  if (ADS131M08_StartReadAllChannelDataDma() == 0U)
  {
    g_daq_ctx.adc_ready_pending++;
    g_daq_ctx.dropped_buffers++;
  }
}

static void DAQ_MaskDrdyIrqForStorage(void)
{
  /*
   * Keep SDMMC/FatFs writes deterministic by blocking new DRDY ISR entries
   * during the write. Do not abort SPI DMA and do not clear adc_pending; an
   * in-flight ADC DMA can still complete and the pending counter remains a
   * diagnostic of capture pressure.
   */
  HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
}

static void DAQ_UnmaskDrdyIrqAfterStorage(void)
{
  __HAL_GPIO_EXTI_CLEAR_IT(ADS_DRDY_Pin);
  HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static uint8_t DAQ_CountSelectedChannels(void)
{
  uint8_t count = 0U;

  for (uint8_t channel = 0U; channel < DAQ_CHANNEL_COUNT; channel++)
  {
    if ((g_daq_cfg.channel_mask & (1UL << channel)) != 0U)
    {
      count++;
    }
  }

  return count;
}

static uint32_t DAQ_GetQueuedBytes(uint16_t sample_count)
{
  if (g_daq_mode == DAQ_MODE_LOG_TO_EMMC)
  {
    return (uint32_t)sample_count * (uint32_t)DAQ_CountSelectedChannels() * sizeof(int32_t);
  }

  return (uint32_t)sample_count * sizeof(DaqSampleFrame_t);
}

static void DAQ_WriteS32Le(uint8_t *data, int32_t value)
{
  uint32_t raw = (uint32_t)value;

  data[0] = (uint8_t)raw;
  data[1] = (uint8_t)(raw >> 8);
  data[2] = (uint8_t)(raw >> 16);
  data[3] = (uint8_t)(raw >> 24);
}

static uint32_t DAQ_CalibrationChecksum(const DaqOffsetCalibrationFile_t *record)
{
  uint32_t checksum;

  if (record == NULL)
  {
    return 0U;
  }

  checksum = record->magic ^ record->version ^ record->channel_count;
  for (uint8_t channel = 0U; channel < DAQ_OFFSET_CAL_CHANNELS; channel++)
  {
    checksum ^= (uint32_t)record->offset[channel];
    checksum = (checksum << 5) | (checksum >> 27);
  }

  return checksum;
}

static void DAQ_ApplyZeroOffsetCalibration(void)
{
  for (uint8_t channel = 0U; channel < DAQ_OFFSET_CAL_CHANNELS; channel++)
  {
    calibrate(0, channel);
  }
}

static void DAQ_StartOffsetCalibrationAccumulator(void)
{
  memset(g_offset_calibration_sum, 0, sizeof(g_offset_calibration_sum));
  g_offset_calibration_count = 0U;
  g_calibration_samples_averaged = 0U;
  g_offset_calibration_ignore_until = HAL_GetTick() + DAQ_OFFSET_CAL_IGNORE_MS;
  g_offset_calibration_average_until = g_offset_calibration_ignore_until + DAQ_OFFSET_CAL_AVERAGE_MS;
  g_offset_calibration_active = 1U;
}

static uint8_t DAQ_AccumulateOffsetCalibrationFrame(const adc_channel_data *raw)
{
  uint32_t now;

  if (raw == NULL)
  {
    return 0U;
  }

  now = HAL_GetTick();
  if ((int32_t)(now - g_offset_calibration_average_until) >= 0)
  {
    g_offset_calibration_active = 0U;
    return 1U;
  }

  if ((int32_t)(now - g_offset_calibration_ignore_until) < 0)
  {
    return 1U;
  }

  g_offset_calibration_sum[0] += raw->channel0;
  g_offset_calibration_sum[1] += raw->channel1;
  g_offset_calibration_sum[2] += raw->channel2;
  g_offset_calibration_sum[3] += raw->channel3;
  g_offset_calibration_sum[4] += raw->channel4;
  g_offset_calibration_sum[5] += raw->channel5;
  g_offset_calibration_count++;
  g_daq_ctx.samples_captured++;
  return 1U;
}

static uint32_t DAQ_PackBlockSelectedChannels(const DaqWriteBlock_t *blk,
                                              uint8_t *buffer,
                                              uint32_t buffer_size)
{
  uint32_t offset = 0U;

  if ((blk == NULL) || (buffer == NULL))
  {
    return 0U;
  }

  for (uint16_t sample = 0U; sample < blk->sample_count; sample++)
  {
    for (uint8_t channel = 0U; channel < DAQ_CHANNEL_COUNT; channel++)
    {
      if ((g_daq_cfg.channel_mask & (1UL << channel)) != 0U)
      {
        if ((offset + sizeof(int32_t)) > buffer_size)
        {
          return 0U;
        }

        DAQ_WriteS32Le(&buffer[offset], blk->sample[sample].channel[channel]);
        offset += sizeof(int32_t);
      }
    }
  }

  return offset;
}

uint32_t DAQ_ServiceAdcPending(uint32_t max_events)
{
  uint32_t processed = 0U;

  if (max_events == 0U)
  {
    return 0U;
  }

  while (processed < max_events)
  {
    uint8_t has_event = 0U;

    __disable_irq();
    if (g_daq_ctx.adc_ready_pending != 0U)
    {
      g_daq_ctx.adc_ready_pending--;
      has_event = 1U;
    }
    __enable_irq();

    if (has_event == 0U)
    {
      break;
    }

    if (ADS131M08_StartReadAllChannelDataDma() == 0U)
    {
      __disable_irq();
      g_daq_ctx.adc_ready_pending++;
      __enable_irq();
      break;
    }

    processed++;
  }

  return processed;
}

uint32_t DAQ_ServiceCapturedSamples(uint32_t max_samples)
{
  uint32_t processed = 0U;
  adc_channel_data raw;

  if (max_samples == 0U)
  {
    return 0U;
  }

  while ((processed < max_samples) && (ADS131M08_TakeDmaFrame(&raw) != 0U))
  {
    (void)DAQ_StoreAdcFrame(&raw);
    processed++;
  }

  return processed;
}

uint8_t DAQ_ShouldDeferBackgroundWork(void)
{
  return (uint8_t)((g_daq_ctx.state == DAQ_STATE_ACQUIRING) &&
                   ((g_daq_ctx.adc_ready_pending != 0U) ||
                    (ADS131M08_IsReadDmaBusy() != 0U)));
}

void DAQ_ServicePendingWrites(void)
{
  DaqWriteBlock_t blk;
  uint32_t blk_bytes;
#if (DAQ_EMMC_WRITE_ENABLE != 0U)
  uint32_t bytes_written;
#endif

  if (g_daq_mode != DAQ_MODE_LOG_TO_EMMC)
  {
    return;
  }

  if (DAQ_QueuePop(&blk) == 0U)
  {
    return;
  }

  blk_bytes = DAQ_GetQueuedBytes(blk.sample_count);
#if (DAQ_EMMC_WRITE_ENABLE != 0U)
  blk_bytes = DAQ_PackBlockSelectedChannels(&blk, emmc_pack_buffer, sizeof(emmc_pack_buffer));
  if (blk_bytes == 0U)
  {
    g_daq_ctx.last_error = DAQ_ERROR_WRITE_DATA;
    g_daq_last_op_status = (int32_t)EMMC_FS_ERR_PARAM;
    g_daq_ctx.dropped_buffers++;
    return;
  }

  DAQ_MaskDrdyIrqForStorage();

  if ((EmmcFs_IsRawLogOpen() == 0U) ||
      (EmmcFs_WriteRawLog(emmc_pack_buffer,
                          blk_bytes,
                          &bytes_written) != EMMC_FS_OK) ||
      (bytes_written != blk_bytes))
  {
    g_daq_ctx.last_error = DAQ_ERROR_WRITE_DATA;
    g_daq_last_op_status = (int32_t)EMMC_FS_ERR_READ_FILE;
    g_daq_ctx.dropped_buffers++;
    DAQ_UnmaskDrdyIrqAfterStorage();
    return;
  }

  DAQ_UnmaskDrdyIrqAfterStorage();
#endif

  if (g_daq_ctx.bytes_queued >= (uint64_t)blk_bytes)
  {
    g_daq_ctx.bytes_queued -= (uint64_t)blk_bytes;
  }
  else
  {
    g_daq_ctx.bytes_queued = 0U;
  }
  g_daq_ctx.bytes_written += (uint64_t)blk_bytes;
}

uint8_t DAQ_HasPendingWrites(void)
{
  uint8_t pending = (uint8_t)((write_q_count != 0U) || (aggr_block.sample_count != 0U));

  if ((g_daq_mode == DAQ_MODE_STREAM_TO_SHMEM) && (g_daq_ctx.is_adc_armed == 0U))
  {
    DAQ_ResetSoftwareBuffers();
    g_daq_mode = DAQ_MODE_IDLE;
    return 0U;
  }

  if ((pending == 0U) && (g_daq_ctx.is_adc_armed == 0U))
  {
    if (EmmcFs_IsRawLogOpen() != 0U)
    {
      DAQ_MaskDrdyIrqForStorage();
      if (EmmcFs_CloseRawLog() != EMMC_FS_OK)
      {
        g_daq_ctx.last_error = DAQ_ERROR_WRITE_DATA;
        g_daq_last_op_status = (int32_t)EMMC_FS_ERR_READ_FILE;
      }
      DAQ_UnmaskDrdyIrqAfterStorage();
    }
    g_daq_mode = DAQ_MODE_IDLE;
  }

  return pending;
}
