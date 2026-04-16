#include "ads131m08.h"


//****************************************************************************
//
// Internal variables
//
//****************************************************************************

// Array to recall device registers
static uint16_t registerMap[NUM_REGISTERS];

// Array of SPI word lengths
const static uint8_t  wlength_byte_values[] = {2, 3, 4, 4};


//****************************************************************************
//
// Internal function prototypes
//
//****************************************************************************
uint8_t     buildSPIarray(const uint16_t opcodeArray[], uint8_t numberOpcodes, uint8_t byteArray[]);
uint16_t    enforce_selected_device_modes(uint16_t data);
uint8_t     getWordByteLength(void);

#define ADS131M08_FRAME_WORDS       (CHANNEL_COUNT + 2U)
#define ADS131M08_BOOT_WORD_BYTES   (3U)
#define ADS131M08_MAX_FRAME_BYTES   (ADS131M08_FRAME_WORDS * 4U)

static uint16_t sendBootFullFrameCommand(uint16_t opcode);
static uint16_t normalizeDeviceIdPattern(uint16_t raw_id);


uint16_t getDeviceIdPattern(){
	uint16_t response =  readSingleRegister(ID_ADDRESS);
	return (((response & 0xFF00) >> 8) | ((response & 0x00FF) << 8)) & 0xFF00;
}


uint16_t detectBootDeviceIdPattern(void)
{
	uint16_t raw_id = 0U;
	uint16_t normalized_id = 0U;

	HAL_Delay(10);
	setSYNC_RESET(HIGH);
	toggleRESET();

	restoreRegisterDefaults();
	(void)sendBootFullFrameCommand(OPCODE_RESET);
	HAL_Delay(10);

	/* Clear the reset/status response pipeline before issuing RREG. */
	(void)sendBootFullFrameCommand(OPCODE_NULL);
	(void)sendBootFullFrameCommand(OPCODE_NULL);

	for (uint32_t attempt = 0U; attempt < 5U; attempt++)
	{
		HAL_Delay(10U);

		(void)sendBootFullFrameCommand(OPCODE_RREG | (((uint16_t)ID_ADDRESS) << 7));
		raw_id = sendBootFullFrameCommand(OPCODE_NULL);
		normalized_id = normalizeDeviceIdPattern(raw_id);

		if (normalized_id == (ID_DEFAULT & 0xFF00U))
		{
			break;
		}
	}

	registerMap[ID_ADDRESS] = raw_id;
	return normalized_id;
}


uint16_t getRegisterValue(uint8_t address)
{
    return registerMap[address];
}


void setSYNC_RESET(bool state) {
	if(state) {
		HAL_GPIO_WritePin(SYNC_RESET_GPIO_Port, SYNC_RESET_Pin, GPIO_PIN_SET);
	} else {
		HAL_GPIO_WritePin(SYNC_RESET_GPIO_Port, SYNC_RESET_Pin, GPIO_PIN_RESET);
	}
}


void toggleRESET() {
	// Set LOW and then to HIGH
	HAL_GPIO_WritePin(SYNC_RESET_GPIO_Port, SYNC_RESET_Pin, GPIO_PIN_RESET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(SYNC_RESET_GPIO_Port, SYNC_RESET_Pin, GPIO_PIN_SET);
}


void adcStartup(void) {

	HAL_Delay(10);

	// Reset the ADC at the beginning
	setSYNC_RESET(HIGH);
	// Toggle the reset pin
	toggleRESET();

	resetDevice();
	HAL_Delay(10);

	sendCommand(OPCODE_NULL);
	sendCommand(OPCODE_NULL);

	writeSingleRegister(CLOCK_ADDRESS, (CLOCK_DEFAULT & ~CLOCK_OSR_MASK) | CLOCK_OSR_256);
}


uint16_t readSingleRegister(uint8_t address)
{

	// Built TX and RX byte array
#ifdef ENABLE_CRC_IN
	uint8_t dataTx[8] = {0};
	uint8_t dataRx[8] = {0};

#else
	// Build TX and RX byte array
	uint8_t dataTx[4] = { 0 };      // 1 word, up to 4 bytes long = 4 bytes maximum
	uint8_t dataRx[4] = { 0 };
#endif

	uint16_t opcode = OPCODE_RREG | (((uint16_t) address) << 7);
	uint8_t numberOfBytes = buildSPIarray(&opcode, 1, dataTx);

	// [FRAME 1] Send RREG command
	spiSendReceiveArrays(dataTx, dataRx, numberOfBytes);

	// [FRAME 2] Send NULL command to retrieve the register data
	registerMap[address] = sendCommand(OPCODE_NULL);

	return registerMap[address];
}


void writeSingleRegister(uint8_t address, uint16_t data) {

	if (MODE_ADDRESS == address) {
		data = enforce_selected_device_modes(data);
	}

#ifdef ENABLE_CRC_IN
	uint8_t dataTx[12] = {0};
	uint8_t dataRx[12] = {0};
#else
	uint8_t dataTx[8] = {0};
	uint8_t dataRx[8] = {0};
#endif
	uint16_t opcodes[2];
	opcodes[0] = OPCODE_WREG | (((uint16_t) address) << 7);
	opcodes[1] = data;
	uint8_t numberOfBytes = buildSPIarray(&opcodes[0], 2, dataTx);

	// Send command
	spiSendReceiveArrays(dataTx, dataRx, numberOfBytes);

	// Update Internal Array
	registerMap[address] = data;

	// configure register write
	readSingleRegister(address);
}


void spiSendReceiveArrays(const uint8_t dataTx[], uint8_t dataRx[], const uint8_t byteLength) {
	ADS131M08_CS_LOW();

	int i;
	for (i = 0; i < byteLength; i++)
	{
		dataRx[i] = spiSendReceiveByte(dataTx[i]);
	}

	ADS131M08_CS_HIGH();
}


void readAllChannelData(adc_channel_data *DataStruct) {
    uint8_t bytesPerWord = getWordByteLength();
    uint8_t totalWords = 10;  // Response (1) + 8 Channels + CRC (1)
    uint16_t bufferSize = totalWords * bytesPerWord;

    // Create SPI buffers for sending and receiving
    uint8_t txBuffer[bufferSize];
    uint8_t rxBuffer[bufferSize];

    // Fill TX buffer with NULL words (or CRC if enabled)
    memset(txBuffer, 0x00, bufferSize);

#ifdef ENABLE_CRC_IN
    // Compute CRC input
    uint16_t crcWordIn = calculateCRC(txBuffer, bufferSize - bytesPerWord, 0xFFFF);
    txBuffer[bufferSize - bytesPerWord] = upperByte(crcWordIn);
    txBuffer[bufferSize - bytesPerWord + 1] = lowerByte(crcWordIn);
#endif

    ADS131M08_CS_LOW();

    // Perform a single SPI transaction to receive all data
    HAL_SPI_TransmitReceive(ads.hspi, txBuffer, rxBuffer, bufferSize, HAL_MAX_DELAY);

    ADS131M08_CS_HIGH();

    // Process received data
    DataStruct->response = combineBytes(rxBuffer[0], rxBuffer[1]);

    // Assign values to channels
    DataStruct->channel0 = signExtend(&rxBuffer[1 * bytesPerWord]);
    DataStruct->channel1 = signExtend(&rxBuffer[2 * bytesPerWord]);
    DataStruct->channel2 = signExtend(&rxBuffer[3 * bytesPerWord]);
    DataStruct->channel3 = signExtend(&rxBuffer[4 * bytesPerWord]);
    DataStruct->channel4 = signExtend(&rxBuffer[5 * bytesPerWord]);
    DataStruct->channel5 = signExtend(&rxBuffer[6 * bytesPerWord]);
    DataStruct->channel6 = signExtend(&rxBuffer[7 * bytesPerWord]);
    DataStruct->channel7 = signExtend(&rxBuffer[8 * bytesPerWord]);

    // Read CRC word
	DataStruct->crc = combineBytes(rxBuffer[9 * bytesPerWord],
								   rxBuffer[9 * bytesPerWord + 1]);

}


void calibrate(int32_t adc_value, uint8_t channel) {
	// Ensure the ADC value is within the 24-bit range
	if (adc_value > 0x7FFFFF) {
		adc_value = 0x7FFFFF;  // Max positive value of int24_t
	} else if (adc_value < -0x800000) {
		adc_value = -0x800000; // Max negative value of int24_t
	}

	// Proper sign extension for 24-bit format
	if (adc_value & 0x800000) {  // Check if the 24th bit is set (negative number)
		adc_value |= ~0xFFFFFF;  // Proper sign extension
	} else {
		adc_value &= 0x00FFFFFF;  // Keep only lower 24 bits for positive numbers
	}

	// Extract the 24-bit value from the 32-bit int
	uint8_t msb = (adc_value >> 16) & 0xFF; // Most Significant Byte
	uint8_t mid = (adc_value >> 8) & 0xFF;  // Middle Byte
	uint8_t lsb = adc_value & 0xFF;         // Least Significant Byte

	// Combine MSB and Mid bytes into a 16-bit word
	uint16_t msb_word = (msb << 8) | mid;
	uint16_t lsb_word = lsb << 8; // LSB is a separate register

	// Define calibration register addresses for each channel
	uint8_t ocal_msb_address;
	uint8_t ocal_lsb_address;
	switch (channel) {
		case 0:
			ocal_msb_address = CH0_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH0_OCAL_LSB_ADDRESS;
			break;

		case 1:
			ocal_msb_address = CH1_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH1_OCAL_LSB_ADDRESS;
			break;

		case 2:
			ocal_msb_address = CH2_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH2_OCAL_LSB_ADDRESS;
			break;

		case 3:
			ocal_msb_address = CH3_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH3_OCAL_LSB_ADDRESS;
			break;

		case 4:
			ocal_msb_address = CH4_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH4_OCAL_LSB_ADDRESS;
			break;

		case 5:
			ocal_msb_address = CH5_OCAL_MSB_ADDRESS;
			ocal_lsb_address = CH5_OCAL_LSB_ADDRESS;
			break;

		default:
			break;

	}

	// Write to the ADS131M08 calibration registers
	writeSingleRegister(ocal_msb_address, msb_word);
	writeSingleRegister(ocal_lsb_address, lsb_word);
}


bool readData(adc_channel_data *DataStruct) {
	int i;
	uint8_t crcTx[4]                        = { 0 };
	uint8_t dataRx[4]                       = { 0 };
	uint8_t bytesPerWord                    = getWordByteLength();

#ifdef ENABLE_CRC_IN
    // Build CRC word (only if "RX_CRC_EN" register bit is enabled)
    uint16_t crcWordIn = calculateCRC(&DataTx[0], bytesPerWord * 2, 0xFFFF);
    crcTx[0] = upperByte(crcWordIn);
    crcTx[1] = lowerByte(crcWordIn);
#endif

    ADS131M08_CS_LOW();

    // Send NULL word, receive response word
    for (i = 0; i < bytesPerWord; i++) {
		dataRx[i] = spiSendReceiveByte(0x00);
	}
    DataStruct->response = combineBytes(dataRx[0], dataRx[1]);

    // (OPTIONAL) Do something with the response (STATUS) word.
	// ...Here we only use the response for calculating the CRC-OUT
	//uint16_t crcWord = calculateCRC(&dataRx[0], bytesPerWord, 0xFFFF);

	// (OPTIONAL) Ignore CRC error checking
	uint16_t crcWord = 0;

	// Send 2nd word, receive channel 1 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(crcTx[i]);
	}
	DataStruct->channel0 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#if (CHANNEL_COUNT > 1)

	// Send 3rd word, receive channel 2 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel1 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 2)

	// Send 4th word, receive channel 3 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel2 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 3)

	// Send 5th word, receive channel 4 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel3 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 4)

	// Send 6th word, receive channel 5 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel4 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 5)

	// Send 7th word, receive channel 6 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel5 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 6)

	// Send 8th word, receive channel 7 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel6 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif
#if (CHANNEL_COUNT > 7)

	// Send 9th word, receive channel 8 data
	for (i = 0; i < bytesPerWord; i++)
	{
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->channel7 = signExtend(&dataRx[0]);
	//crcWord = calculateCRC(&dataRx[0], bytesPerWord, crcWord);

#endif

	// Send the next word, receive CRC data
	for (i = 0; i < bytesPerWord; i++) {
		dataRx[i] = spiSendReceiveByte(0x00);
	}
	DataStruct->crc = combineBytes(dataRx[0], dataRx[1]);

	ADS131M08_CS_HIGH();

	return ((bool) crcWord);

}


uint16_t sendCommand(uint16_t opcode) {
    // Build TX and RX byte array
#ifdef ENABLE_CRC_IN
    uint8_t dataTx[8] = { 0 };      // 2 words, up to 4 bytes each = 8 bytes maximum
    uint8_t dataRx[8] = { 0 };
#else
    uint8_t dataTx[4] = { 0 };      // 1 word, up to 4 bytes long = 4 bytes maximum
    uint8_t dataRx[4] = { 0 };
#endif

    uint8_t numberOfBytes = buildSPIarray(&opcode, 1, dataTx);

    ADS131M08_CS_LOW();
    // Send the opcode (and crc word, if enabled)
	int i;
	for (i = 0; i < numberOfBytes; i++) {
	   dataRx[i] = spiSendReceiveByte(dataTx[i]);
	}
	ADS131M08_CS_HIGH();

	// Combine response bytes and return as a 16-bit word
	uint16_t adcResponse = combineBytes(dataRx[0], dataRx[1]);
	return adcResponse;
}


void resetDevice(void) {
	// Build TX and RX byte array
#ifdef ENABLE_CRC_IN
	uint8_t dataTx[8] = { 0 };      // 2 words, up to 4 bytes each = 8 bytes maximum
	//uint8_t dataRx[8] = { 0 };    // Only needed if capturing data
#else
	uint8_t dataTx[4] = { 0 };      // 1 word, up to 4 bytes long = 4 bytes maximum
	//uint8_t dataRx[4] = { 0 };    // Only needed if capturing data
#endif

	uint16_t opcode         = OPCODE_RESET;
	uint8_t numberOfBytes   = buildSPIarray(&opcode, 1, dataTx);

	uint8_t bytesPerWord    = wlength_byte_values[WLENGTH];
	uint8_t wordsInFrame    = CHANNEL_COUNT + 2;

	ADS131M08_CS_LOW();

	 // Send the opcode (and CRC word, if enabled)
	int i;
	for (i = 0; i < numberOfBytes; i++)
	{
		 spiSendReceiveByte(dataTx[i]);
	}

	// Finish sending remaining bytes
	for (i = numberOfBytes; i < (wordsInFrame * bytesPerWord); i++)
	{
		spiSendReceiveByte(0x00);
	}

	// NOTE: The ADS131M0x's next response word should be (0xFF20 | CHANCNT),
	// if the response is 0x0011 (acknowledge of RESET command), then the device
	// did not receive a full SPI frame and the reset did not occur!

	ADS131M08_CS_HIGH();

	HAL_Delay(1);

	// Update register setting array to keep software in sync with device
	restoreRegisterDefaults();

	// Write to MODE register to enforce mode settings
	writeSingleRegister(MODE_ADDRESS, MODE_DEFAULT);

}


bool lockRegisters(void) {

	bool b_lock_error;

	// Build TX and RX byte array
#ifdef ENABLE_CRC_IN
	uint8_t dataTx[8] = { 0 };      // 2 words, up to 4 bytes each = 8 bytes maximum
	uint8_t dataRx[8] = { 0 };
#else
	uint8_t dataTx[4] = { 0 };      // 1 word, up to 4 bytes long = 4 bytes maximum
	uint8_t dataRx[4] = { 0 };
#endif
	uint16_t opcode         = OPCODE_LOCK;
	uint8_t numberOfBytes   = buildSPIarray(&opcode, 1, dataTx);

	// Send command
	spiSendReceiveArrays(dataTx, dataRx, numberOfBytes);

	/* (OPTIONAL) Check for SPI errors by sending the NULL command and checking STATUS */

	/* (OPTIONAL) Read back the STATUS register and check if LOCK bit is set... */
	readSingleRegister(STATUS_ADDRESS);
	if (!SPI_LOCKED) { b_lock_error = true; }

	/* If the STATUS register is NOT read back,
	 * then make sure to manually update the global register map variable... */
	//registerMap[STATUS_ADDRESS]  |= STATUS_LOCK_LOCKED;

	/* (OPTIONAL) Error handler */
	if (b_lock_error)
	{
		// Insert error handler function call here...
	}

	return b_lock_error;
}


bool unlockRegisters(void) {
	bool b_unlock_error;

	// Build TX and RX byte array
#ifdef ENABLE_CRC_IN
	uint8_t dataTx[8] = { 0 };      // 2 words, up to 4 bytes each = 8 bytes maximum
	uint8_t dataRx[8] = { 0 };
#else
	uint8_t dataTx[4] = { 0 };      // 1 word, up to 4 bytes long = 4 bytes maximum
	uint8_t dataRx[4] = { 0 };
#endif
	uint16_t opcode = OPCODE_UNLOCK;
	uint8_t numberOfBytes = buildSPIarray(&opcode, 1, dataTx);

	// Send command
	spiSendReceiveArrays(dataTx, dataRx, numberOfBytes);

	/* (OPTIONAL) Check for SPI errors by sending the NULL command and checking STATUS */

	/* (OPTIONAL) Read the STATUS register and check if LOCK bit is cleared... */
	readSingleRegister(STATUS_ADDRESS);
	if (SPI_LOCKED) { b_unlock_error = true; }

	/* If the STATUS register is NOT read back,
	 * then make sure to manually update the global register map variable... */
	//registerMap[STATUS_ADDRESS]  &= !STATUS_LOCK_LOCKED;

	/* (OPTIONAL) Error handler */
	if (b_unlock_error)
	{
		// Insert error handler function call here...
	}

	return b_unlock_error;
}


uint16_t calculateCRC(const uint8_t dataBytes[], uint8_t numberBytes, uint16_t initialValue)
{

	int         bitIndex, byteIndex;
	bool        dataMSb;						/* Most significant bit of data byte */
	bool        crcMSb;						    /* Most significant bit of crc byte  */
	uint8_t     bytesPerWord = wlength_byte_values[WLENGTH];

	/*
     * Initial value of crc register
     * NOTE: The ADS131M0x defaults to 0xFFFF,
     * but can be set at function call to continue an on-going calculation
     */
    uint16_t crc = initialValue;

    #ifdef CRC_CCITT
    /* CCITT CRC polynomial = x^16 + x^12 + x^5 + 1 */
    const uint16_t poly = 0x1021;
    #endif

    #ifdef CRC_ANSI
    /* ANSI CRC polynomial = x^16 + x^15 + x^2 + 1 */
    const uint16_t poly = 0x8005;
    #endif

    //
    // CRC algorithm
    //

    // Loop through all bytes in the dataBytes[] array
	for (byteIndex = 0; byteIndex < numberBytes; byteIndex++)
	{
	    // Point to MSb in byte
	    bitIndex = 0x80u;

	    // Loop through all bits in the current byte
	    while (bitIndex > 0)
	    {
	        // Check MSB's of data and crc
	        dataMSb = (bool) (dataBytes[byteIndex] & bitIndex);
	        crcMSb  = (bool) (crc & 0x8000u);

	        crc <<= 1;              /* Left shift CRC register */

	        // Check if XOR operation of MSBs results in additional XOR operations
	        if (dataMSb ^ crcMSb)
	        {
	            crc ^= poly;        /* XOR crc with polynomial */
	        }

	        /* Shift MSb pointer to the next data bit */
	        bitIndex >>= 1;
	    }
	}

	return crc;
}


void restoreRegisterDefaults(void)
{
    registerMap[ID_ADDRESS]             =   0x00;               /* NOTE: This a read-only register */
    registerMap[STATUS_ADDRESS]         =   STATUS_DEFAULT;
    registerMap[MODE_ADDRESS]           =   MODE_DEFAULT;
    registerMap[CLOCK_ADDRESS]          =   CLOCK_DEFAULT;
    registerMap[GAIN1_ADDRESS]          =   GAIN1_DEFAULT;
    registerMap[GAIN2_ADDRESS]          =   GAIN2_DEFAULT;
    registerMap[CFG_ADDRESS]            =   CFG_DEFAULT;
    registerMap[THRSHLD_MSB_ADDRESS]    =   THRSHLD_MSB_DEFAULT;
    registerMap[THRSHLD_LSB_ADDRESS]    =   THRSHLD_LSB_DEFAULT;
    registerMap[CH0_CFG_ADDRESS]        =   CH0_CFG_DEFAULT;
    registerMap[CH0_OCAL_MSB_ADDRESS]   =   CH0_OCAL_MSB_DEFAULT;
    registerMap[CH0_OCAL_LSB_ADDRESS]   =   CH0_OCAL_LSB_DEFAULT;
    registerMap[CH0_GCAL_MSB_ADDRESS]   =   CH0_GCAL_MSB_DEFAULT;
    registerMap[CH0_GCAL_LSB_ADDRESS]   =   CH0_GCAL_LSB_DEFAULT;
#if (CHANNEL_COUNT > 1)
    registerMap[CH1_CFG_ADDRESS]        =   CH1_CFG_DEFAULT;
    registerMap[CH1_OCAL_MSB_ADDRESS]   =   CH1_OCAL_MSB_DEFAULT;
    registerMap[CH1_OCAL_LSB_ADDRESS]   =   CH1_OCAL_LSB_DEFAULT;
    registerMap[CH1_GCAL_MSB_ADDRESS]   =   CH1_GCAL_MSB_DEFAULT;
    registerMap[CH1_GCAL_LSB_ADDRESS]   =   CH1_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 2)
    registerMap[CH2_CFG_ADDRESS]        =   CH2_CFG_DEFAULT;
    registerMap[CH2_OCAL_MSB_ADDRESS]   =   CH2_OCAL_MSB_DEFAULT;
    registerMap[CH2_OCAL_LSB_ADDRESS]   =   CH2_OCAL_LSB_DEFAULT;
    registerMap[CH2_GCAL_MSB_ADDRESS]   =   CH2_GCAL_MSB_DEFAULT;
    registerMap[CH2_GCAL_LSB_ADDRESS]   =   CH2_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 3)
    registerMap[CH3_CFG_ADDRESS]        =   CH3_CFG_DEFAULT;
    registerMap[CH3_OCAL_MSB_ADDRESS]   =   CH3_OCAL_MSB_DEFAULT;
    registerMap[CH3_OCAL_LSB_ADDRESS]   =   CH3_OCAL_LSB_DEFAULT;
    registerMap[CH3_GCAL_MSB_ADDRESS]   =   CH3_GCAL_MSB_DEFAULT;
    registerMap[CH3_GCAL_LSB_ADDRESS]   =   CH3_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 4)
    registerMap[CH4_CFG_ADDRESS]        =   CH4_CFG_DEFAULT;
    registerMap[CH4_OCAL_MSB_ADDRESS]   =   CH4_OCAL_MSB_DEFAULT;
    registerMap[CH4_OCAL_LSB_ADDRESS]   =   CH4_OCAL_LSB_DEFAULT;
    registerMap[CH4_GCAL_MSB_ADDRESS]   =   CH4_GCAL_MSB_DEFAULT;
    registerMap[CH4_GCAL_LSB_ADDRESS]   =   CH4_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 5)
    registerMap[CH5_CFG_ADDRESS]        =   CH5_CFG_DEFAULT;
    registerMap[CH5_OCAL_MSB_ADDRESS]   =   CH5_OCAL_MSB_DEFAULT;
    registerMap[CH5_OCAL_LSB_ADDRESS]   =   CH5_OCAL_LSB_DEFAULT;
    registerMap[CH5_GCAL_MSB_ADDRESS]   =   CH5_GCAL_MSB_DEFAULT;
    registerMap[CH5_GCAL_LSB_ADDRESS]   =   CH5_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 6)
    registerMap[CH6_CFG_ADDRESS]        =   CH6_CFG_DEFAULT;
    registerMap[CH6_OCAL_MSB_ADDRESS]   =   CH6_OCAL_MSB_DEFAULT;
    registerMap[CH6_OCAL_LSB_ADDRESS]   =   CH6_OCAL_LSB_DEFAULT;
    registerMap[CH6_GCAL_MSB_ADDRESS]   =   CH6_GCAL_MSB_DEFAULT;
    registerMap[CH6_GCAL_LSB_ADDRESS]   =   CH6_GCAL_LSB_DEFAULT;
#endif
#if (CHANNEL_COUNT > 7)
    registerMap[CH7_CFG_ADDRESS]        =   CH7_CFG_DEFAULT;
    registerMap[CH7_OCAL_MSB_ADDRESS]   =   CH7_OCAL_MSB_DEFAULT;
    registerMap[CH7_OCAL_LSB_ADDRESS]   =   CH7_OCAL_LSB_DEFAULT;
    registerMap[CH7_GCAL_MSB_ADDRESS]   =   CH7_GCAL_MSB_DEFAULT;
    registerMap[CH7_GCAL_LSB_ADDRESS]   =   CH7_GCAL_LSB_DEFAULT;
#endif
    registerMap[REGMAP_CRC_ADDRESS]     =   REGMAP_CRC_DEFAULT;
}


//****************************************************************************
//
// Helper functions
//
//****************************************************************************


uint8_t upperByte(uint16_t uint16_Word)
{
    uint8_t msByte;
    msByte = (uint8_t) ((uint16_Word >> 8) & 0x00FF);

    return msByte;
}


uint8_t lowerByte(uint16_t uint16_Word)
{
    uint8_t lsByte;
    lsByte = (uint8_t) (uint16_Word & 0x00FF);

    return lsByte;
}


uint16_t combineBytes(uint8_t upperByte, uint8_t lowerByte)
{
    uint16_t combinedValue;
    combinedValue = ((uint16_t) upperByte << 8) | ((uint16_t) lowerByte);

    return combinedValue;
}


int32_t signExtend(const uint8_t dataBytes[])
{

#ifdef WORD_LENGTH_24BIT

    int32_t upperByte   = ((int32_t) dataBytes[0] << 24);
    int32_t middleByte  = ((int32_t) dataBytes[1] << 16);
    int32_t lowerByte   = ((int32_t) dataBytes[2] << 8);

    return (((int32_t) (upperByte | middleByte | lowerByte)) >> 8);     // Right-shift of signed data maintains signed bit

#elif defined WORD_LENGTH_32BIT_SIGN_EXTEND

    int32_t signByte    = ((int32_t) dataBytes[0] << 24);
    int32_t upperByte   = ((int32_t) dataBytes[1] << 16);
    int32_t middleByte  = ((int32_t) dataBytes[2] << 8);
    int32_t lowerByte   = ((int32_t) dataBytes[3] << 0);

    return (signByte | upperByte | middleByte | lowerByte);

#elif defined WORD_LENGTH_32BIT_ZERO_PADDED

    int32_t upperByte   = ((int32_t) dataBytes[0] << 24);
    int32_t middleByte  = ((int32_t) dataBytes[1] << 16);
    int32_t lowerByte   = ((int32_t) dataBytes[2] << 8);

    return (((int32_t) (upperByte | middleByte | lowerByte)) >> 8);     // Right-shift of signed data maintains signed bit

#elif defined WORD_LENGTH_16BIT_TRUNCATED

    int32_t upperByte   = ((int32_t) dataBytes[0] << 24);
    int32_t lowerByte   = ((int32_t) dataBytes[1] << 16);

    return (((int32_t) (upperByte | lowerByte)) >> 16);                 // Right-shift of signed data maintains signed bit

#endif
}


//****************************************************************************
//
// Internal functions
//
//****************************************************************************


uint8_t buildSPIarray(const uint16_t opcodeArray[], uint8_t numberOpcodes, uint8_t byteArray[])
{
    /*
     * Frame size = opcode word(s) + optional CRC word
     * Number of bytes per word = 2, 3, or 4
     * Total bytes = bytes per word * number of words
     */
    uint8_t numberWords     = numberOpcodes + (SPI_CRC_ENABLED ? 1 : 0);
    uint8_t bytesPerWord    = getWordByteLength();
    uint8_t numberOfBytes   = numberWords * bytesPerWord;

    int i;
    for (i = 0; i < numberOpcodes; i++)
    {
        // NOTE: Be careful not to accidentally overflow the array here.
        // The array and opcodes are defined in the calling function, so
        // we are trusting that no mistakes were made in the calling function!
        byteArray[(i*bytesPerWord) + 0] = upperByte(opcodeArray[i]);
        byteArray[(i*bytesPerWord) + 1] = lowerByte(opcodeArray[i]);
    }

#ifdef ENABLE_CRC_IN
    // Calculate CRC and put it into TX array
    uint16_t crcWord = calculateCRC(&byteArray[0], numberOfBytes, 0xFFFF);
    byteArray[(i*bytesPerWord) + 0] = upperByte(crcWord);
    byteArray[(i*bytesPerWord) + 1] = lowerByte(crcWord);
#endif

    return numberOfBytes;
}


uint16_t enforce_selected_device_modes(uint16_t data)
{


    ///////////////////////////////////////////////////////////////////////////
    // Enforce RX_CRC_EN setting

#ifdef ENABLE_CRC_IN
    // When writing to the MODE register, ensure RX_CRC_EN bit is ALWAYS set
    data |= MODE_RX_CRC_EN_ENABLED;
#else
    // When writing to the MODE register, ensure RX_CRC_EN bit is NEVER set
    data &= ~MODE_RX_CRC_EN_ENABLED;
#endif // ENABLE_CRC_IN


    ///////////////////////////////////////////////////////////////////////////
    // Enforce WLENGH setting

#ifdef WORD_LENGTH_24BIT
    // When writing to the MODE register, ensure WLENGTH bits are ALWAYS set to 01b
    data = (data & ~MODE_WLENGTH_MASK) | MODE_WLENGTH_24BIT;
#elif defined WORD_LENGTH_32BIT_SIGN_EXTEND
    // When writing to the MODE register, ensure WLENGH bits are ALWAYS set to 11b
    data = (data & ~MODE_WLENGTH_MASK) | MODE_WLENGTH_32BIT_MSB_SIGN_EXT;
#elif defined WORD_LENGTH_32BIT_ZERO_PADDED
    // When writing to the MODE register, ensure WLENGH bits are ALWAYS set to 10b
    data = (data & ~MODE_WLENGTH_MASK) | MODE_WLENGTH_32BIT_LSB_ZEROES;
#elif defined WORD_LENGTH_16BIT_TRUNCATED
    // When writing to the MODE register, ensure WLENGH bits are ALWAYS set to 00b
    data = (data & ~MODE_WLENGTH_MASK) | MODE_WLENGTH_16BIT;
#endif


    ///////////////////////////////////////////////////////////////////////////
    // Enforce DRDY_FMT setting

#ifdef DRDY_FMT_PULSE
    // When writing to the MODE register, ensure DRDY_FMT bit is ALWAYS set
    data = (data & ~MODE_DRDY_FMT_MASK) | MODE_DRDY_FMT_NEG_PULSE_FIXED_WIDTH;
#else
    // When writing to the MODE register, ensure DRDY_FMT bit is NEVER set
    data = (data & ~MODE_DRDY_FMT_MASK) | MODE_DRDY_FMT_LOGIC_LOW;
#endif


    ///////////////////////////////////////////////////////////////////////////
    // Enforce CRC_TYPE setting

#ifdef CRC_CCITT
    // When writing to the MODE register, ensure CRC_TYPE bit is NEVER set
    data = (data & ~STATUS_CRC_TYPE_MASK) | STATUS_CRC_TYPE_16BIT_CCITT;
#elif defined CRC_ANSI
    // When writing to the MODE register, ensure CRC_TYPE bit is ALWAYS set
    data = (data & ~STATUS_CRC_TYPE_MASK) | STATUS_CRC_TYPE_16BIT_ANSI;
#endif

    // Return modified register data
    return data;
}


uint8_t getWordByteLength(void)
{
    return wlength_byte_values[WLENGTH];
}

static uint16_t sendBootFullFrameCommand(uint16_t opcode)
{
	uint8_t dataTx[ADS131M08_MAX_FRAME_BYTES] = {0};
	uint8_t dataRx[ADS131M08_MAX_FRAME_BYTES] = {0};
	uint16_t frameBytes = ADS131M08_FRAME_WORDS * ADS131M08_BOOT_WORD_BYTES;

	dataTx[0] = upperByte(opcode);
	dataTx[1] = lowerByte(opcode);

	ADS131M08_CS_LOW();
	HAL_SPI_TransmitReceive(ads.hspi, dataTx, dataRx, frameBytes, HAL_MAX_DELAY);
	ADS131M08_CS_HIGH();

	return combineBytes(dataRx[0], dataRx[1]);
}

static uint16_t normalizeDeviceIdPattern(uint16_t raw_id)
{
	uint16_t direct = raw_id & 0xFF00U;
	uint16_t swapped = (((raw_id & 0xFF00U) >> 8) | ((raw_id & 0x00FFU) << 8)) & 0xFF00U;

	if (direct == (ID_DEFAULT & 0xFF00U))
	{
		return direct;
	}

	return swapped;
}


uint8_t spiSendReceiveByte(const uint8_t dataTx)
{

    // SSI TX & RX
    uint8_t dataRx;

    HAL_SPI_TransmitReceive(ads.hspi, &dataTx, &dataRx, 1, HAL_MAX_DELAY);

    return dataRx;
}



