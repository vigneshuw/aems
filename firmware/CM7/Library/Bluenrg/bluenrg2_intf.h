#ifndef _BLUENRG2_INTF_H
#define _BLUENRG2_INTF_H

#include "bluenrg1_types.h"
#include "bluenrg_conf.h"
#include "bluenrg1_gap_aci.h"
#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "hci.h"


#define BLE_MAX_CHUNK_SIZE		30			// Max without memory errors

/*
 * Control Triggers
 */
// TODO: Change the DAQ variable to be declared here
extern uint8_t stream_active;
extern uint32_t stream_counter;


// Characteristics update
typedef enum {
	STATUS_DAQ = 0,
	STATUS_STREAM,
	STATUS_VPh1,
	STATUS_VPh2,
	STATUS_VPh3,
	STATUS_IPh1,
	STATUS_IPh2,
	STATUS_IPh3,
	STATUS_RS485,
	STATUS_INTF,
} StatusCharacteristic;


tBleStatus BlueNRG_Init(void);
tBleStatus BlueNRG_StartAdvertising(void);
tBleStatus BlueNRG_ConfigureMTU(uint16_t conn_handle);
void aci_att_exchange_mtu_resp_event(uint16_t Connection_Handle, uint16_t Server_RX_MTU);
uint8_t BlueNRG_GetStatus(void);
void BlueNRG_Process(void);
int8_t BlueNRG_UpdateStatusCharacteristic(StatusCharacteristic characteristic, int32_t value);
int8_t BlueNRG_SendStreamChunk(uint8_t *data, uint16_t length);
int8_t BlueNRG_SendLargeBuffer(uint8_t *buffer, uint32_t length);
void BlueNRG_AddStreamService(void);


/*
 * Control Helper Functions
 */
uint8_t* get_control_daq_value(void);
uint8_t* get_control_rs485_value(void);
void get_DaqFilename(uint8_t *filename_buffer, uint8_t buffer_size);
uint8_t* getStreamFilename(void);


void BlueNRG_AddServices(void);
void BlueNRG_UserEvtRx(void *pData);

#endif /* _BLUENRG2_INTF_H */
