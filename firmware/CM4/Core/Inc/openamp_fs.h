#ifndef OPENAMP_FS_H
#define OPENAMP_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t OpenAmpFs_RemoteInit(void);
void OpenAmpFs_RemotePoll(void);
int32_t OpenAmpFs_GetRemoteInitStatus(void);
uint32_t OpenAmpFs_GetRemoteRxCount(void);

#ifdef __cplusplus
}
#endif

#endif
