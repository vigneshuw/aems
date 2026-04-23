#ifndef OPENAMP_FS_H
#define OPENAMP_FS_H

#include <stdint.h>
#include "daq_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

int32_t OpenAmpFs_MasterInit(void);
int32_t OpenAmpFs_Ping(uint32_t request_value, uint32_t *reply_value);
int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count);
int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count);
int32_t OpenAmpFs_ListFilesShared(uint8_t **buffer, uint32_t *bytes_read);
int32_t OpenAmpFs_DeleteLogFiles(uint32_t *deleted_count);
int32_t OpenAmpFs_DeleteFile(const char *filename, uint32_t *deleted_count);
int32_t OpenAmpFs_GetFileSize(const char *filename, uint32_t *file_size);
int32_t OpenAmpFs_ReadFileChunk(const char *filename,
                                uint32_t offset,
                                uint8_t *buffer,
                                uint16_t buffer_size,
                                uint16_t *bytes_read,
                                uint32_t *total_size);
int32_t OpenAmpFs_OpenFileStream(const char *filename, uint32_t offset, uint32_t *file_size);
int32_t OpenAmpFs_ReadFileStream(uint8_t *buffer,
                                 uint16_t buffer_size,
                                 uint16_t *bytes_read,
                                 uint32_t *offset,
                                 uint32_t *total_size);
int32_t OpenAmpFs_ReadFileStreamShared(uint8_t **buffer,
                                       uint16_t buffer_size,
                                       uint16_t *bytes_read,
                                       uint32_t *offset,
                                       uint32_t *total_size);
int32_t OpenAmpFs_ProbeSharedMemory(uint32_t *probe_len, uint32_t *bad_index);
int32_t OpenAmpFs_CloseFileStream(void);
int32_t OpenAmpFs_DaqGetStatus(DaqStatus_t *status);
int32_t OpenAmpFs_DaqStartLog(const DaqConfig_t *config);
int32_t OpenAmpFs_DaqStartStream(const DaqConfig_t *config);
int32_t OpenAmpFs_DaqReadStreamShared(uint8_t **buffer,
                                      uint16_t buffer_size,
                                      uint16_t *bytes_read,
                                      uint32_t *samples_read,
                                      uint32_t *samples_captured);
int32_t OpenAmpFs_DaqStop(void);
int32_t OpenAmpFs_DaqStopAndClose(void);
int32_t OpenAmpFs_DaqGetCalibration(DaqCalibration_t *calibration);
int32_t OpenAmpFs_DaqRunCalibration(DaqCalibration_t *calibration);
uint32_t OpenAmpFs_GetServiceCreated(void);
uint32_t OpenAmpFs_GetRxCount(void);
int32_t OpenAmpFs_GetInitStatus(void);
int32_t OpenAmpFs_GetRemoteInitStatus(void);
int32_t OpenAmpFs_GetRemoteMountStatus(void);
uint32_t OpenAmpFs_GetRemoteAdcDeviceId(void);

#ifdef __cplusplus
}
#endif

#endif
