#ifndef OPENAMP_FS_H
#define OPENAMP_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t OpenAmpFs_MasterInit(void);
int32_t OpenAmpFs_CountDatFiles(uint32_t *dat_count);
int32_t OpenAmpFs_CountAllFiles(uint32_t *file_count);

#ifdef __cplusplus
}
#endif

#endif
