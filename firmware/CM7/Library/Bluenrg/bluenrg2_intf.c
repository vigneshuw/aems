#include "bluenrg2_intf.h"
#include "hci_tl.h"
#include "hci_const.h"
#include "bluenrg1_gatt_aci.h"
#include "hci.h"
#include <stdio.h>

static uint8_t ble_initialized = 0;
static uint8_t advertising = 0;
static uint8_t connected = 0;
static uint8_t ble_error = 0;

/*
 * Service Handles
 */
static uint16_t control_service_handle, status_service_handle, control_stream_file_handle;
// Streaming Service
static uint16_t stream_service_handle = 0;
static uint16_t stream_data_char_handle = 0;

/*
 * Characteristics Handle
 */
// Control characteristics
static uint16_t control_daq_handle, control_rs485_handle, control_daq_fn_handle;
// Status Characteristic Handles
static uint16_t status_daq_handle, status_stream_handle, status_vph1_handle, status_vph2_handle, status_vph3_handle, status_iph1_handle, status_iph2_handle, status_iph3_handle;

/*
 * Control Characteristics Data
 */
static uint8_t control_daq_value = 0;
static uint8_t control_rs485_value = 0;
static uint8_t control_daq_fn_value[20] = {0};

// Streaming
uint8_t stream_active = 0;
uint32_t stream_counter = 0;
static uint8_t control_stream_file_value[20] = {0}; 	// Streaming filename


/*
 * MTU Exchange
 */
static uint16_t negotiated_mtu = 23;
static uint8_t mtu_exchange_done = 0;
static uint8_t mtu_exchange_error = 0;

tBleStatus BlueNRG_Init(void) {
    hci_init(BlueNRG_UserEvtRx, NULL);  // Ensure HCI is initialized
    hci_reset();
    HAL_Delay(100);

    uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
	tBleStatus ret = aci_gatt_init();
	if (ret != BLE_STATUS_SUCCESS) {
		ble_error = 1;
		return ret;
	}

	ret = aci_gap_init(GAP_PERIPHERAL_ROLE, 0, 0x07, &service_handle, &dev_name_char_handle, &appearance_char_handle);
	if (ret != BLE_STATUS_SUCCESS) {
		ble_error = 1;
		return ret;
	}

	// Setup all services
	BlueNRG_AddServices();

	ble_initialized = 1;

	return BLE_STATUS_SUCCESS;
}


tBleStatus BlueNRG_ConfigureMTU(uint16_t conn_handle) {
    return aci_gatt_exchange_config(conn_handle);
}

void aci_att_exchange_mtu_resp_event(uint16_t Connection_Handle, uint16_t Server_RX_MTU) {
    negotiated_mtu = Server_RX_MTU;
    mtu_exchange_done = 1;
}


tBleStatus BlueNRG_StartAdvertising(void) {

	if(!ble_initialized || ble_error) return BLE_STATUS_NOT_ALLOWED;

    if (advertising) {
    	aci_gap_set_non_discoverable();
        return BLE_STATUS_ERROR;
    }

    uint8_t local_name[] = {AD_TYPE_COMPLETE_LOCAL_NAME, 'A', 'E', 'M', 'S', '-', '0', '0', '1'};
    tBleStatus ret = aci_gap_set_discoverable(
        ADV_IND,         // Advertising type: Connectable undirected
        0x0020,          // Min advertising interval (20ms)
        0x0040,          // Max advertising interval (40ms)
        PUBLIC_ADDR,     // Address type
        NO_WHITE_LIST_USE, // No white-list filtering
        sizeof(local_name), local_name,
        0, NULL, 0, 0
    );

    if (ret != BLE_STATUS_SUCCESS) {
    	ble_error = 1;
    	return ret;
    }

    advertising = 1;
    return BLE_STATUS_SUCCESS;
}


void BlueNRG_AddServices(void) {
    tBleStatus ret;

    Service_UUID_t control_service_uuid, status_service_uuid;
    control_service_uuid.Service_UUID_16 = 0x1100;
	status_service_uuid.Service_UUID_16 = 0x1200;


    // ✅ Add Control Service
    ret = aci_gatt_add_service(UUID_TYPE_16, &control_service_uuid, PRIMARY_SERVICE, 24, &control_service_handle);

    if (ret == BLE_STATUS_SUCCESS) {
        // Add Control Characteristics (Boolean)
    	Char_UUID_t control_daq_uuid;
		control_daq_uuid.Char_UUID_16 = 0x1101;
        ret = aci_gatt_add_char(control_service_handle, UUID_TYPE_16, &control_daq_uuid,
                                1, CHAR_PROP_READ | CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
                                GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &control_daq_handle);

        Char_UUID_t control_rs485_uuid;
		control_rs485_uuid.Char_UUID_16 = 0x1102;
        ret = aci_gatt_add_char(control_service_handle, UUID_TYPE_16, &control_rs485_uuid,
                                1, CHAR_PROP_READ | CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
                                GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &control_rs485_handle);

        // Get filename
        Char_UUID_t control_daq_fn_uuid;
        control_daq_fn_uuid.Char_UUID_16 = 0x1103;
        ret = aci_gatt_add_char(control_service_handle, UUID_TYPE_16, &control_daq_fn_uuid,
               20, CHAR_PROP_READ | CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
                GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &control_daq_fn_handle);

        // Trigger FileStreaming with Filename
        Char_UUID_t control_stream_file_uuid;
        control_stream_file_uuid.Char_UUID_16 = 0x1104;
        ret = aci_gatt_add_char(control_service_handle, UUID_TYPE_16, &control_stream_file_uuid,
        		20, CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
				GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &control_stream_file_handle);
    }


    // ✅ Add Status Service
    ret = aci_gatt_add_service(UUID_TYPE_16, &status_service_uuid, PRIMARY_SERVICE, 28, &status_service_handle);

    if (ret == BLE_STATUS_SUCCESS) {
        // Add 5 Status Characteristics
    	Char_UUID_t status_daq_uuid;
    	status_daq_uuid.Char_UUID_16 = 0x1201;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_daq_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_daq_handle);

        Char_UUID_t status_vph1_uuid;
		status_vph1_uuid.Char_UUID_16 = 0x1202;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_vph1_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_vph1_handle);

        Char_UUID_t status_vph2_uuid;
		status_vph2_uuid.Char_UUID_16 = 0x1203;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_vph2_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_vph2_handle);

        Char_UUID_t status_vph3_uuid;
		status_vph3_uuid.Char_UUID_16 = 0x1204;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_vph3_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_vph3_handle);

        Char_UUID_t status_iph1_uuid;
		status_iph1_uuid.Char_UUID_16 = 0x1205;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_iph1_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_iph1_handle);

        Char_UUID_t status_iph2_uuid;
		status_iph2_uuid.Char_UUID_16 = 0x1206;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_iph2_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_iph2_handle);

        Char_UUID_t status_iph3_uuid;
		status_iph3_uuid.Char_UUID_16 = 0x1207;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_iph3_uuid,
                                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &status_iph3_handle);

        Char_UUID_t status_stream_uuid;
        status_stream_uuid.Char_UUID_16 = 0x120A;
        ret = aci_gatt_add_char(status_service_handle, UUID_TYPE_16, &status_stream_uuid,
                4, CHAR_PROP_READ | CHAR_PROP_NOTIFY | CHAR_PROP_INDICATE, ATTR_PERMISSION_NONE, 0, 16, 1, &status_stream_handle);
    }


    // ✅ Add Streaming Service
    BlueNRG_AddStreamService();

}


void aci_gatt_attribute_modified_event(uint16_t Connection_Handle, uint16_t Attr_Handle,
                                       uint16_t Offset, uint16_t Attr_Data_Length,
                                       uint8_t Attr_Data[]) {
    if (Attr_Handle == control_daq_handle + 1) {
        control_daq_value = Attr_Data[0];  // Store boolean value
    } else if (Attr_Handle == control_rs485_handle + 1) {
        control_rs485_value = Attr_Data[0];  // Store boolean value
    } else if (Attr_Handle == control_daq_fn_handle + 1) {
    	memset(control_daq_fn_value, 0, sizeof(control_daq_fn_value));
    	memcpy(control_daq_fn_value, Attr_Data, 20);
    } else if (Attr_Handle == control_stream_file_handle + 1) {
        memset(control_stream_file_value, 0, sizeof(control_stream_file_value));
        memcpy(control_stream_file_value, Attr_Data, Attr_Data_Length);
        stream_active = 1;  // Trigger stream state
    }
}


int8_t BlueNRG_UpdateStatusCharacteristic(StatusCharacteristic characteristic, int32_t value){
	uint16_t char_handle = 0;

	switch (characteristic) {
		case STATUS_DAQ:
			char_handle = status_daq_handle;
			break;
		case STATUS_VPh1:
			char_handle = status_vph1_handle;
			break;
		case STATUS_VPh2:
			char_handle = status_vph2_handle;
			break;
		case STATUS_VPh3:
			char_handle = status_vph3_handle;
			break;
		case STATUS_IPh1:
			char_handle = status_iph1_handle;
			break;
		case STATUS_IPh2:
			char_handle = status_iph2_handle;
			break;
		case STATUS_IPh3:
			char_handle = status_iph3_handle;
			break;
		case STATUS_STREAM:
			char_handle = status_stream_handle;
			break;

		default:
			return -1;  // Invalid characteristic
	}

	if (char_handle == 0) return -2;  // Ignore invalid handles

	uint8_t buffer[4];
	memcpy(buffer, &value, sizeof(value));

	tBleStatus ret = aci_gatt_update_char_value(status_service_handle, char_handle, 0, sizeof(buffer), buffer);
	if (ret != BLE_STATUS_SUCCESS) {
		return -3;
	}

	return 0;
}


void BlueNRG_AddStreamService(void) {
	tBleStatus ret;

	Service_UUID_t stream_service_uuid;
	stream_service_uuid.Service_UUID_16 = 0x1300;

	ret = aci_gatt_add_service(UUID_TYPE_16, &stream_service_uuid, PRIMARY_SERVICE,
							   6, &stream_service_handle);

	if (ret != BLE_STATUS_SUCCESS) {
		return;
	}

	Char_UUID_t stream_char_uuid;
	stream_char_uuid.Char_UUID_16 = 0x1301;

	ret = aci_gatt_add_char(
		stream_service_handle,
		UUID_TYPE_16,
		&stream_char_uuid,
		30,  // Max BLE chunk size
		CHAR_PROP_NOTIFY,
		ATTR_PERMISSION_NONE,
		0,   // GATT_EVENT_NONE
		16, 1,
		&stream_data_char_handle
	);
}


int8_t BlueNRG_SendStreamChunk(uint8_t *data, uint16_t length){
	if(stream_data_char_handle == 0 || length == 0 || length > BLE_MAX_CHUNK_SIZE)
		return -1;

	tBleStatus ret = aci_gatt_update_char_value(
		stream_service_handle,
		stream_data_char_handle,
		0,  // offset
		length,
		data
	);
	stream_counter++;

	return (ret == BLE_STATUS_SUCCESS) ? 0 : -2;
}


int8_t BlueNRG_SendLargeBuffer(uint8_t *buffer, uint32_t length){
	if(!buffer || length ==0) return -1;

	uint32_t offset = 0;
	while(offset < length){
		uint16_t chunk_len = (length - offset > BLE_MAX_CHUNK_SIZE) ? BLE_MAX_CHUNK_SIZE : (length - offset);

		int8_t ret = BlueNRG_SendStreamChunk(&buffer[offset], chunk_len);
		if(ret != 0){
			return ret;
		}

		offset += chunk_len;

		// Optional delay
		HAL_Delay(100);
	}

	return 0;
}


uint8_t* get_control_daq_value(void) {
	return &control_daq_value;
}


uint8_t* get_control_rs485_value(void) {
	return &control_rs485_value;
}

void get_DaqFilename(uint8_t *filename_buffer, uint8_t buffer_size){
	if(filename_buffer == NULL || buffer_size < sizeof(control_daq_fn_value)) {
		return;
	}
	memcpy(filename_buffer, control_daq_fn_value, sizeof(control_daq_fn_value));
}


uint8_t* getStreamFilename(void) {
    return control_stream_file_value;
}


void BlueNRG_UserEvtRx(void *pData) {

	uint32_t i;

	hci_spi_pckt *hci_pckt = (hci_spi_pckt *)pData;

	if(hci_pckt->type == HCI_EVENT_PKT) {
	    hci_event_pckt *event_pckt = (hci_event_pckt*)hci_pckt->data;

		if(event_pckt->evt == EVT_LE_META_EVENT) {
		  evt_le_meta_event *evt = (void *)event_pckt->data;

		  for (i = 0; i < (sizeof(hci_le_meta_events_table)/sizeof(hci_le_meta_events_table_type)); i++) {
			if (evt->subevent == hci_le_meta_events_table[i].evt_code) {
			  hci_le_meta_events_table[i].process((void *)evt->data);
			}
		  }
		}

		else if(event_pckt->evt == EVT_VENDOR) {
			evt_blue_aci *blue_evt = (void*)event_pckt->data;

			for (i = 0; i < (sizeof(hci_vendor_specific_events_table)/sizeof(hci_vendor_specific_events_table_type)); i++) {
				if (blue_evt->ecode == hci_vendor_specific_events_table[i].evt_code) {
				  hci_vendor_specific_events_table[i].process((void *)blue_evt->data);
				}
			}
		}

		else {
		  for (i = 0; i < (sizeof(hci_events_table)/sizeof(hci_events_table_type)); i++) {
			if (event_pckt->evt == hci_events_table[i].evt_code) {
			  hci_events_table[i].process((void *)event_pckt->data);
			}
		  }
		}
	}

}


void BlueNRG_Process(void) {
    hci_user_evt_proc();  // Process BLE events
}


uint8_t BlueNRG_GetStatus(void) {
	if (ble_error) return BLE_STATUS_ERROR;
	if (connected || advertising || ble_initialized) return BLE_STATUS_SUCCESS;

	return BLE_STATUS_NOT_ALLOWED;
}





void hci_le_connection_complete_event(uint8_t Status, uint16_t Connection_Handle, uint8_t Role,
        uint8_t Peer_Address_Type, uint8_t Peer_Address[6],
        uint16_t Conn_Interval, uint16_t Conn_Latency,
        uint16_t Supervision_Timeout, uint8_t Master_Clock_Accuracy) {
	if (Status == BLE_STATUS_SUCCESS) {
		connected = 1;
		advertising = 0;

		if (BlueNRG_ConfigureMTU(Connection_Handle) != BLE_STATUS_SUCCESS) {
			mtu_exchange_error = 1;
		}

	} else {
		ble_error = 1;
	}
}


void hci_disconnection_complete_event(uint8_t Status, uint16_t Connection_Handle, uint8_t Reason) {
    connected = 0;
    HAL_Delay(500);
    BlueNRG_StartAdvertising(); // Restart advertising when disconnected
}
