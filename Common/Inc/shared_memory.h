#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ipc_shared.h"

#define SHARED_IPC_REGION_BASE_ADDR   (0x30000000UL)

#define SHARED_IPC_REGION   ((volatile SharedIpcRegion_t *)SHARED_IPC_REGION_BASE_ADDR)

#ifdef __cplusplus
}
#endif

#endif /* SHARED_MEMORY_H */
