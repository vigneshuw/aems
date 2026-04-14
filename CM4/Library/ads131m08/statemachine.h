/*
 * statemachine.h
 *
 * CM4 DAQ state machine public interface.
 */

#ifndef INC_STATEMACHINE_H_
#define INC_STATEMACHINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define DAQ_EVT_CMD_START   (1UL << 0)
#define DAQ_EVT_CMD_STOP    (1UL << 1)
#define DAQ_EVT_ADC_READY   (1UL << 2)

/**
  * @brief Initialize DAQ runtime context fields to a known reset state.
  * @param None
  * @return None
  */
void DAQ_ContextInit(void);

/**
  * @brief Execute one iteration of the CM4 DAQ state machine.
  * @param None
  * @return None
  */
void DAQ_StateMachine_Run(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_STATEMACHINE_H_ */
