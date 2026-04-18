#include "statemachine.h"
#include "daq_engine.h"

static void DAQ_SetError(uint32_t err)
{
  /* Keep a single latched error code and move to terminal DAQ error state. */
  g_daq_ctx.last_error = err;
  g_daq_ctx.state = DAQ_STATE_ERROR;
}

void DAQ_ContextInit(void)
{
  g_daq_ctx.state = DAQ_STATE_IDLE;
  g_daq_ctx.events = 0U;
  g_daq_ctx.last_error = 0U;
  g_daq_ctx.samples_captured = 0U;
  g_daq_ctx.dropped_buffers = 0U;
  g_daq_ctx.bytes_queued = 0U;
  g_daq_ctx.bytes_written = 0U;
  g_daq_ctx.adc_ready_pending = 0U;
  g_daq_ctx.is_adc_armed = 0U;
}

void DAQ_StateMachine_Run(void)
{
  /* Single-step cooperative state machine; called from the CM4 superloop. */
  switch (g_daq_ctx.state)
  {
    case DAQ_STATE_IDLE:
      if ((g_daq_ctx.events & DAQ_EVT_CMD_START) != 0U)
      {
        g_daq_ctx.events &= ~DAQ_EVT_CMD_START;
        g_daq_ctx.state = DAQ_STATE_PREPARING;
      }
      else
      {
        g_daq_ctx.events = 0U;
      }
      break;

    case DAQ_STATE_PREPARING:
      /* Hardware bring-up is done once here, then acquisition starts. */
      DAQ_Startup();
      if (g_daq_ctx.is_adc_armed == 0U)
      {
        DAQ_SetError(1U);
      }
      else
      {
        g_daq_ctx.state = DAQ_STATE_ACQUIRING;
      }
      break;

    case DAQ_STATE_ACQUIRING:
      if ((g_daq_ctx.events & DAQ_EVT_CMD_STOP) != 0U)
      {
        g_daq_ctx.events &= ~DAQ_EVT_CMD_STOP;
        g_daq_ctx.state = DAQ_STATE_STOPPING;
      }
      else if (g_daq_ctx.adc_ready_pending != 0U)
      {
        __disable_irq();
        if (g_daq_ctx.adc_ready_pending != 0U)
        {
          g_daq_ctx.adc_ready_pending--;
        }
        __enable_irq();

        if (DAQ_ProcessAdcReadyEvent() == 0U)
        {
          DAQ_SetError(3U);
        }
      }
      DAQ_ServicePendingWrites();
      break;

    case DAQ_STATE_STOPPING:
      DAQ_Shutdown();
      g_daq_ctx.state = DAQ_STATE_FINALIZING;
      break;

    case DAQ_STATE_FINALIZING:
      /* Drain queued blocks before declaring the run closed. */
      DAQ_ServicePendingWrites();
      if (DAQ_HasPendingWrites() == 0U)
      {
        g_daq_ctx.state = DAQ_STATE_IDLE;
      }
      break;

    case DAQ_STATE_ERROR:
      if ((g_daq_ctx.events & DAQ_EVT_CMD_STOP) != 0U)
      {
        g_daq_ctx.events &= ~DAQ_EVT_CMD_STOP;
        DAQ_Shutdown();
        g_daq_ctx.state = DAQ_STATE_IDLE;
      }
      break;

    default:
      DAQ_SetError(2U);
      break;
  }
}
