#ifndef OPENAMP_FS_H
#define OPENAMP_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t OpenAmpFs_MasterInit(void);
int32_t OpenAmpFs_Ping(uint32_t request_value, uint32_t *reply_value);
int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count);
int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count);
int32_t OpenAmpFs_GetFileSize(const char *filename, uint32_t *file_size);
uint32_t OpenAmpFs_GetServiceCreated(void);
uint32_t OpenAmpFs_GetRxCount(void);
int32_t OpenAmpFs_GetInitStatus(void);
int32_t OpenAmpFs_GetRemoteInitStatus(void);
int32_t OpenAmpFs_GetRemoteMountStatus(void);

#ifdef __cplusplus
}
#endif

#endif
