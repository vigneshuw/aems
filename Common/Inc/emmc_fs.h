#ifndef EMMC_FS_H
#define EMMC_FS_H

#include <stdint.h>
#include "ff.h"

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
    EMMC_FS_ERR_READ_DIR = -7,
    EMMC_FS_ERR_OPEN_FILE = -8,
    EMMC_FS_ERR_READ_FILE = -9
} EmmcFsStatus_t;

typedef enum
{
    EMMC_FS_CREATE_STAGE_NONE = 0,
    EMMC_FS_CREATE_STAGE_MOUNT = 1,
    EMMC_FS_CREATE_STAGE_OPEN = 2,
    EMMC_FS_CREATE_STAGE_WRITE = 3,
    EMMC_FS_CREATE_STAGE_SYNC = 4
} EmmcFsCreateStage_t;

typedef struct
{
    uint32_t total_file_count;
    uint32_t dat_file_count;
} EmmcFsDatSummary_t;

typedef struct
{
    FIL file;
    uint32_t total_size;
    uint8_t is_open;
} EmmcFsReadHandle_t;

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
 * @brief Rewrite `config_main.conf` with server ID, epoch time, and payload bytes.
 * @param server_id Server ID value to write in big-endian order.
 * @param epoch_time Epoch time value to write in big-endian order.
 * @param payload Pointer to the payload bytes to append after the header.
 * @param payload_len Number of payload bytes to write.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_PARAM` if `payload` is `NULL` while `payload_len` is non-zero.
 * @return `EMMC_FS_ERR_OPEN_DIR` if the config file cannot be opened for write.
 * @return `EMMC_FS_ERR_READ_DIR` if a write operation fails.
 */
EmmcFsStatus_t EmmcFs_WriteConfigMain(uint32_t server_id,
                                      uint64_t epoch_time,
                                      const uint8_t *payload,
                                      uint16_t payload_len);

/**
 * @brief Create or rewrite a file with deterministic patterned data.
 * @param filename Null-terminated file name or path to create.
 * @param file_size Total file size in bytes.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_PARAM` if `filename` is `NULL`.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_OPEN_FILE` if the file cannot be opened for write.
 * @return `EMMC_FS_ERR_READ_FILE` if a write or sync operation fails.
 */
EmmcFsStatus_t EmmcFs_CreatePatternFile(const char *filename,
                                       uint32_t file_size,
                                       uint32_t *fail_offset,
                                       uint8_t *fail_stage);

/**
 * @brief Count all files in the eMMC root directory.
 * @param file_count Pointer to the output file count.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_PARAM` if `file_count` is `NULL`.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_OPEN_DIR` if the root directory cannot be opened.
 * @return `EMMC_FS_ERR_READ_DIR` if directory enumeration fails.
 */
EmmcFsStatus_t EmmcFs_CountAllFiles(uint32_t *file_count);

/**
 * @brief Read a chunk from `config_main.conf`.
 * @param offset Byte offset in the file to read from.
 * @param buffer Destination buffer for file data.
 * @param buffer_size Maximum number of bytes to place into `buffer`.
 * @param bytes_read Pointer to the number of bytes read.
 * @param total_size Pointer to the total file size in bytes.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_PARAM` if output pointers are invalid.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_OPEN_FILE` if `config_main.conf` cannot be opened.
 * @return `EMMC_FS_ERR_READ_FILE` if seek or read fails.
 */
EmmcFsStatus_t EmmcFs_ReadConfigMainChunk(uint32_t offset,
                                          uint8_t *buffer,
                                          uint16_t buffer_size,
                                          uint16_t *bytes_read,
                                          uint32_t *total_size);

/**
 * @brief Read a chunk from a specific file in the eMMC filesystem.
 * @param filename Null-terminated file name or path to read.
 * @param offset Byte offset in the file to read from.
 * @param buffer Destination buffer for file data.
 * @param buffer_size Maximum number of bytes to place into `buffer`.
 * @param bytes_read Pointer to the number of bytes read.
 * @param total_size Pointer to the total file size in bytes.
 * @return `EMMC_FS_OK` on success.
 * @return `EMMC_FS_ERR_PARAM` if input or output pointers are invalid.
 * @return `EMMC_FS_ERR_LINK` if the FatFs driver link fails.
 * @return `EMMC_FS_ERR_MOUNT` if the filesystem cannot be mounted.
 * @return `EMMC_FS_ERR_OPEN_FILE` if the file cannot be opened.
 * @return `EMMC_FS_ERR_READ_FILE` if seek or read fails.
 */
EmmcFsStatus_t EmmcFs_ReadFileChunk(const char *filename,
                                    uint32_t offset,
                                    uint8_t *buffer,
                                    uint16_t buffer_size,
                                    uint16_t *bytes_read,
                                    uint32_t *total_size);

EmmcFsStatus_t EmmcFs_OpenFileRead(const char *filename,
                                   EmmcFsReadHandle_t *handle,
                                   uint32_t *total_size);

EmmcFsStatus_t EmmcFs_ReadFileNext(EmmcFsReadHandle_t *handle,
                                   uint8_t *buffer,
                                   uint16_t buffer_size,
                                   uint16_t *bytes_read);

EmmcFsStatus_t EmmcFs_CloseFileRead(EmmcFsReadHandle_t *handle);

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
