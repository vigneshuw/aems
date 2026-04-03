#ifndef EMMC_FS_H
#define EMMC_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    EMMC_FS_OK = 0,
    EMMC_FS_ERR_PARAM = -1,
    EMMC_FS_ERR_LINK = -2,
    EMMC_FS_ERR_MOUNT = -3,
    EMMC_FS_ERR_NO_FS = -4,
    EMMC_FS_ERR_MKFS = -5,
    EMMC_FS_ERR_OPEN_DIR = -6,
    EMMC_FS_ERR_READ_DIR = -7
} EmmcFsStatus_t;

typedef struct
{
    uint32_t dat_file_count;
} EmmcFsDatSummary_t;

/**
 * @brief Link the eMMC FatFs driver for the current core.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 */
EmmcFsStatus_t EmmcFs_Init(void);

/**
 * @brief Mount the eMMC filesystem, or create it if none exists.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if mount fails for a reason other than missing filesystem.
 * @return `EMMC_FS_ERR_MKFS` if formatting or the post-format mount fails.
 */
EmmcFsStatus_t EmmcFs_MountOrFormat(void);

/**
 * @brief Count files ending with `.dat` in the eMMC root directory.
 * @param summary Pointer to the output summary structure.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_PARAM` if `summary` is `NULL`.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_OPEN_DIR` if the root directory cannot be opened.
 * @return `EMMC_FS_ERR_READ_DIR` if directory enumeration fails.
 */
EmmcFsStatus_t EmmcFs_CountDatFiles(EmmcFsDatSummary_t *summary);

#ifdef __cplusplus
}
#endif

#endif
