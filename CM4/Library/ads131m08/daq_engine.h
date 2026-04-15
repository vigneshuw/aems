/*
 * daq_engine.h
 *
 * CM4 DAQ engine control interface.
 */

#ifndef INC_DAQ_ENGINE_H_
#define INC_DAQ_ENGINE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "daq_shared.h"

/**
  * @brief Initialize DAQ engine hardware to a safe idle baseline.
  * @param None
  * @return None
  */
void DAQ_EngineInit(void);

/**
  * @brief Validate and store a DAQ configuration for the next run.
  * @param cfg Pointer to validated candidate configuration.
  * @return uint8_t Returns 1 when configuration is accepted, 0 otherwise.
  */
uint8_t DAQ_ApplyConfig(const DaqConfig_t *cfg);

/**
  * @brief Get the currently applied DAQ configuration.
  * @param None
  * @return const DaqConfig_t* Pointer to CM4-local DAQ configuration.
  */
const DaqConfig_t *DAQ_GetConfig(void);

uint8_t DAQ_StartLogging(const DaqConfig_t *cfg);

uint8_t DAQ_StartStreaming(const DaqConfig_t *cfg);

void DAQ_Stop(void);

void DAQ_GetStatus(DaqStatus_t *status);

uint8_t DAQ_ReadStreamBlockShared(uint8_t **buffer,
                                  uint16_t max_len,
                                  uint16_t *bytes_read,
                                  uint32_t *samples_read);

/**
  * @brief Configure and arm the ADS131M08 DAQ path.
  * @param None
  * @return None
  */
void DAQ_Startup(void);

/**
  * @brief Stop DAQ sampling and update context to a safe idle state.
  * @param None
  * @return None
  */
void DAQ_Shutdown(void);

/**
  * @brief Enable ADC master clock output required by ADS131M08.
  * @param None
  * @return None
  */
void adcMaster_Startup(void);

/**
  * @brief Disable or park ADC master clock output.
  * @param None
  * @return None
  */
void adcMaster_Shutdown(void);

/**
  * @brief Process one ADC-ready event and stage the acquired sample.
  * @param None
  * @return uint8_t Returns 1 on success, 0 on invalid frame or not armed.
  */
uint8_t DAQ_ProcessAdcReadyEvent(void);

/**
  * @brief Service one pending write block from the DAQ queue.
  * @param None
  * @return None
  */
void DAQ_ServicePendingWrites(void);

/**
  * @brief Check if aggregation/write pipeline still has pending data.
  * @param None
  * @return uint8_t Returns 1 when pending data exists, 0 otherwise.
  */
uint8_t DAQ_HasPendingWrites(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_DAQ_ENGINE_H_ */
