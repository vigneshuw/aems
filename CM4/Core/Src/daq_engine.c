#include "daq_engine.h"

#include <string.h>
#include "main.h"
#include "ads131m08.h"

static uint32_t calibration_values[6] = {-3838, -7481, -7500, -491, -4160, 4890};
static DaqSampleFrame_t last_sample;
static DaqConfig_t g_daq_cfg;

#define DAQ_AGGR_SAMPLES_PER_BLOCK   (32U)
#define DAQ_WRITE_QUEUE_DEPTH        (4U)

typedef struct
{
  uint16_t sample_count;
  DaqSampleFrame_t sample[DAQ_AGGR_SAMPLES_PER_BLOCK];
} DaqWriteBlock_t;

static DaqWriteBlock_t aggr_block;
static DaqWriteBlock_t write_queue[DAQ_WRITE_QUEUE_DEPTH];
static uint8_t write_q_head = 0U;
static uint8_t write_q_tail = 0U;
static uint8_t write_q_count = 0U;

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

  if (cfg->filename[IPC_FILENAME_LEN - 1U] != '\0')
  {
    return 0U;
  }

  return 1U;
}

static uint8_t DAQ_QueuePush(const DaqWriteBlock_t *blk)
{
  /* Fixed-depth ring queue to decouple sampling from storage latency. */
  if (write_q_count >= DAQ_WRITE_QUEUE_DEPTH)
  {
    return 0U;
  }

  write_queue[write_q_head] = *blk;
  write_q_head = (uint8_t)((write_q_head + 1U) % DAQ_WRITE_QUEUE_DEPTH);
  write_q_count++;
  g_daq_ctx.bytes_queued += (uint64_t)(blk->sample_count * sizeof(DaqSampleFrame_t));
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
}

void DAQ_Startup(void)
{
  /* Initialize ADC */
  adcMaster_Startup();
  adcStartup();

  /* Configure ADS clocking and PGA gains for the active DAQ profile. */
  writeSingleRegister(CLOCK_ADDRESS, ((CLOCK_DEFAULT & ~(CLOCK_EXTREF_EN_MASK)) | CLOCK_EXTREF_EN_ENABLED));
  writeSingleRegister(CLOCK_ADDRESS, ((CLOCK_DEFAULT & ~(CLOCK_OSR_MASK)) | CLOCK_OSR_1024));
  writeSingleRegister(GAIN1_ADDRESS, ((GAIN1_DEFAULT & ~(GAIN1_PGAGAIN0_MASK | GAIN1_PGAGAIN1_MASK | GAIN1_PGAGAIN2_MASK))) | (GAIN1_PGAGAIN0_4 | GAIN1_PGAGAIN1_4 | GAIN1_PGAGAIN2_4));

  /* Apply calibration values to 6 channels */
  for (uint8_t channel = 0; channel < 6; channel++)
  {
    calibrate(calibration_values[channel], channel);
  }

  /* Requested sample rate is tracked in g_daq_cfg until ADS timing mapping is finalized. */
  g_daq_ctx.is_adc_armed = 1U;
  aggr_block.sample_count = 0U;
}

void DAQ_EngineInit(void)
{
  /* Reset software pipeline state and force ADC clock output to idle-low. */
  g_daq_ctx.is_adc_armed = 0U;
  aggr_block.sample_count = 0U;
  write_q_head = 0U;
  write_q_tail = 0U;
  write_q_count = 0U;
  g_daq_cfg.sample_rate_hz = 4000U;
  g_daq_cfg.channel_mask = 0xFFU;
  g_daq_cfg.block_samples = DAQ_AGGR_SAMPLES_PER_BLOCK;
  g_daq_cfg.flags = 0U;
  (void)strcpy(g_daq_cfg.filename, "daq.bin");
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

  /* Fast guard against obvious invalid SPI frames. */
  if (raw.response == 0xFFFFU)
  {
    g_daq_ctx.dropped_buffers++;
    return 0U;
  }

  last_sample.response = raw.response;
  last_sample.crc = raw.crc;
  last_sample.channel[0] = raw.channel0;
  last_sample.channel[1] = raw.channel1;
  last_sample.channel[2] = raw.channel2;
  last_sample.channel[3] = raw.channel3;
  last_sample.channel[4] = raw.channel4;
  last_sample.channel[5] = raw.channel5;
  last_sample.channel[6] = raw.channel6;
  last_sample.channel[7] = raw.channel7;

  /* L2 buffering: append into aggregation block before queueing to storage path. */
  aggr_block.sample[aggr_block.sample_count++] = last_sample;

  if (aggr_block.sample_count >= g_daq_cfg.block_samples)
  {
    if (DAQ_QueuePush(&aggr_block) == 0U)
    {
      g_daq_ctx.dropped_buffers++;
      aggr_block.sample_count = 0U;
      return 0U;
    }
    aggr_block.sample_count = 0U;
  }

  g_daq_ctx.samples_captured++;

  return 1U;
}

void DAQ_ServicePendingWrites(void)
{
  DaqWriteBlock_t blk;
  uint64_t blk_bytes;

  if (DAQ_QueuePop(&blk) == 0U)
  {
    return;
  }

  /* Placeholder write path: update accounting until eMMC/FatFs writer is wired in. */
  blk_bytes = (uint64_t)(blk.sample_count * sizeof(DaqSampleFrame_t));
  if (g_daq_ctx.bytes_queued >= blk_bytes)
  {
    g_daq_ctx.bytes_queued -= blk_bytes;
  }
  else
  {
    g_daq_ctx.bytes_queued = 0U;
  }
  g_daq_ctx.bytes_written += blk_bytes;
}

uint8_t DAQ_HasPendingWrites(void)
{
  return (uint8_t)((write_q_count != 0U) || (aggr_block.sample_count != 0U));
}
