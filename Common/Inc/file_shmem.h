#ifndef FILE_SHMEM_H
#define FILE_SHMEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CORE_CM7)
#define FILE_SHMEM_BASE_ADDR      0x30000000UL
#elif defined(CORE_CM4)
#define FILE_SHMEM_BASE_ADDR      0x10000000UL
#else
#error "CORE_CM7 or CORE_CM4 must be defined"
#endif

#define FILE_SHMEM_REGION_SIZE    (32U * 1024U)
#define FILE_SHMEM_DATA_OFFSET    0x100U
#define FILE_SHMEM_DATA_LEN       (16U * 1024U)
#define FILE_SHMEM_DATA_ADDR      (FILE_SHMEM_BASE_ADDR + FILE_SHMEM_DATA_OFFSET)

#define FILE_SHMEM_DATA_PTR       ((uint8_t *)FILE_SHMEM_DATA_ADDR)

#ifdef __cplusplus
}
#endif

#endif
