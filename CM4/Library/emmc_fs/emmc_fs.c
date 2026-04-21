#include "emmc_fs.h"

#include "ff.h"
#include "ff_gen_drv.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "mmc_diskio.h"

#include <stdio.h>
#include <string.h>

#define EMMC_FS_PATH_LEN            4U
#define EMMC_FS_FILEPATH_LEN        128U
#define EMMC_FS_WORKBUF_LEN         _MAX_SS
#define EMMC_FS_ROOT_SUFFIX         "/"
#define EMMC_FS_CONFIG_MAIN_PATH    "0:/config_main.conf"
#define EMMC_FS_PATTERN_CHUNK_LEN   4096U
#define EMMC_FS_API_HSEM_ID         HSEM_FS_API_ID

static FATFS s_emmc_fs;
static char s_emmc_path[EMMC_FS_PATH_LEN];
static uint8_t s_driver_linked;
static uint8_t s_fs_mounted;
static uint8_t s_work_buffer[EMMC_FS_WORKBUF_LEN];
static uint8_t s_pattern_chunk[EMMC_FS_PATTERN_CHUNK_LEN];
static EmmcFsWriteHandle_t s_raw_log_handle;

static void EmmcFs_WriteU32Be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void EmmcFs_WriteU64Be(uint8_t *data, uint64_t value)
{
    data[0] = (uint8_t)(value >> 56);
    data[1] = (uint8_t)(value >> 48);
    data[2] = (uint8_t)(value >> 40);
    data[3] = (uint8_t)(value >> 32);
    data[4] = (uint8_t)(value >> 24);
    data[5] = (uint8_t)(value >> 16);
    data[6] = (uint8_t)(value >> 8);
    data[7] = (uint8_t)value;
}

static void EmmcFs_Lock(void)
{
    LOCK_HSEM(EMMC_FS_API_HSEM_ID);
}

static void EmmcFs_Unlock(void)
{
    UNLOCK_HSEM(EMMC_FS_API_HSEM_ID);
}

static EmmcFsStatus_t EmmcFs_EnsureLinked(void)
{
    if (s_driver_linked == 0U)
    {
        if (FATFS_LinkDriver(&MMC_Driver, s_emmc_path) != 0)
        {
            return EMMC_FS_ERR_LINK;
        }

        s_driver_linked = 1U;
    }

    return EMMC_FS_OK;
}

static uint8_t EmmcFs_IsBinOrDatFile(const char *name)
{
    size_t len;
    char c0;
    char c1;
    char c2;
    char c3;

    if (name == NULL)
    {
        return 0U;
    }

    len = strlen(name);
    if (len < 4U)
    {
        return 0U;
    }

    c0 = name[len - 4U];
    c1 = name[len - 3U];
    c2 = name[len - 2U];
    c3 = name[len - 1U];

    if (c0 != '.')
    {
        return 0U;
    }

    if (((c1 == 'd') || (c1 == 'D')) &&
        ((c2 == 'a') || (c2 == 'A')) &&
        ((c3 == 't') || (c3 == 'T')))
    {
        return 1U;
    }

    if (((c1 == 'b') || (c1 == 'B')) &&
        ((c2 == 'i') || (c2 == 'I')) &&
        ((c3 == 'n') || (c3 == 'N')))
    {
        return 1U;
    }

    return 0U;
}

static uint8_t EmmcFs_IsDatFile(const char *name)
{
    size_t len;
    char c0;
    char c1;
    char c2;
    char c3;

    if (name == NULL)
    {
        return 0U;
    }

    len = strlen(name);
    if (len < 4U)
    {
        return 0U;
    }

    c0 = name[len - 4U];
    c1 = name[len - 3U];
    c2 = name[len - 2U];
    c3 = name[len - 1U];

    if (c0 != '.')
    {
        return 0U;
    }

    if ((c1 == 'd') || (c1 == 'D'))
    {
        if ((c2 == 'a') || (c2 == 'A'))
        {
            if ((c3 == 't') || (c3 == 'T'))
            {
                return 1U;
            }
        }
    }

    return 0U;
}

static EmmcFsStatus_t EmmcFs_EnsureMounted(void)
{
    FRESULT result;
    EmmcFsStatus_t status;

    status = EmmcFs_EnsureLinked();
    if (status != EMMC_FS_OK)
    {
        return status;
    }

    if (s_fs_mounted == 0U)
    {
        result = f_mount(&s_emmc_fs, s_emmc_path, 1U);
        if (result != FR_OK)
        {
            return EMMC_FS_ERR_MOUNT;
        }

        s_fs_mounted = 1U;
    }

    return EMMC_FS_OK;
}

static void EmmcFs_FillPattern(uint8_t *buffer, uint32_t offset, uint32_t length)
{
    uint32_t index;

    for (index = 0U; index < length; index++)
    {
        buffer[index] = (uint8_t)((((offset + index) * 37U) + 11U) & 0xFFU);
    }
}

static void EmmcFs_BuildPath(const char *filename, char *path, uint32_t path_len)
{
    if ((filename == NULL) || (path == NULL) || (path_len == 0U))
    {
        return;
    }

    if ((filename[0] != '\0') && (filename[1] == ':'))
    {
        (void)snprintf(path, path_len, "%s", filename);
    }
    else if ((filename[0] == '/') || (filename[0] == '\\'))
    {
        (void)snprintf(path, path_len, "0:%s", filename);
    }
    else
    {
        (void)snprintf(path, path_len, "0:/%s", filename);
    }
}

EmmcFsStatus_t EmmcFs_Init(void)
{
    EmmcFsStatus_t status;

    EmmcFs_Lock();
    status = EmmcFs_EnsureLinked();
    EmmcFs_Unlock();

    return status;
}

EmmcFsStatus_t EmmcFs_MountOrFormat(void)
{
    FRESULT result;
    EmmcFsStatus_t status;

    EmmcFs_Lock();

    status = EmmcFs_EnsureLinked();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    result = f_mount(&s_emmc_fs, s_emmc_path, 1U);
    if (result == FR_OK)
    {
        s_fs_mounted = 1U;
        EmmcFs_Unlock();
        return EMMC_FS_OK;
    }

    if (result != FR_NO_FILESYSTEM)
    {
        s_fs_mounted = 0U;
        EmmcFs_Unlock();
        return EMMC_FS_ERR_MOUNT;
    }

    result = f_mkfs(s_emmc_path, FM_ANY, 0U, s_work_buffer, sizeof(s_work_buffer));
    if (result != FR_OK)
    {
        s_fs_mounted = 0U;
        EmmcFs_Unlock();
        return EMMC_FS_ERR_MKFS;
    }

    result = f_mount(&s_emmc_fs, s_emmc_path, 1U);
    if (result != FR_OK)
    {
        s_fs_mounted = 0U;
        EmmcFs_Unlock();
        return EMMC_FS_ERR_MKFS;
    }

    s_fs_mounted = 1U;
    EmmcFs_Unlock();
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_WriteConfigMain(uint32_t server_id,
                                      uint64_t epoch_time,
                                      const uint8_t *payload,
                                      uint16_t payload_len)
{
    FIL file;
    FRESULT result;
    UINT bytes_written;
    EmmcFsStatus_t status;
    uint8_t header[12];

    if ((payload == NULL) && (payload_len != 0U))
    {
        return EMMC_FS_ERR_PARAM;
    }

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    EmmcFs_WriteU32Be(&header[0], server_id);
    EmmcFs_WriteU64Be(&header[4], epoch_time);

    result = f_open(&file, EMMC_FS_CONFIG_MAIN_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_DIR;
    }

    result = f_write(&file, header, sizeof(header), &bytes_written);
    if ((result != FR_OK) || (bytes_written != sizeof(header)))
    {
        (void)f_close(&file);
        EmmcFs_Unlock();
        return EMMC_FS_ERR_READ_DIR;
    }

    if (payload_len != 0U)
    {
        result = f_write(&file, payload, payload_len, &bytes_written);
        if ((result != FR_OK) || (bytes_written != payload_len))
        {
            (void)f_close(&file);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_DIR;
        }
    }

    result = f_sync(&file);
    (void)f_close(&file);
    EmmcFs_Unlock();

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_DIR;
    }

    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_CreatePatternFile(const char *filename,
                                        uint32_t file_size,
                                        uint32_t *fail_offset,
                                        uint8_t *fail_stage)
{
    FIL file;
    FRESULT result;
    UINT bytes_written;
    uint32_t bytes_remaining;
    uint32_t chunk_len;
    uint32_t offset;
    EmmcFsStatus_t status;
    char path[EMMC_FS_FILEPATH_LEN];

    if (fail_offset != NULL)
    {
        *fail_offset = 0U;
    }

    if (fail_stage != NULL)
    {
        *fail_stage = EMMC_FS_CREATE_STAGE_NONE;
    }

    if (filename == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        if (fail_stage != NULL)
        {
            *fail_stage = EMMC_FS_CREATE_STAGE_MOUNT;
        }
        EmmcFs_Unlock();
        return status;
    }

    memset(path, 0, sizeof(path));
    EmmcFs_BuildPath(filename, path, sizeof(path));

    result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK)
    {
        if (fail_stage != NULL)
        {
            *fail_stage = EMMC_FS_CREATE_STAGE_OPEN;
        }
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_FILE;
    }

    bytes_remaining = file_size;
    offset = 0U;

    while (bytes_remaining > 0U)
    {
        chunk_len = (bytes_remaining > EMMC_FS_PATTERN_CHUNK_LEN) ? EMMC_FS_PATTERN_CHUNK_LEN : bytes_remaining;
        EmmcFs_FillPattern(s_pattern_chunk, offset, chunk_len);

        result = f_write(&file, s_pattern_chunk, chunk_len, &bytes_written);
        if ((result != FR_OK) || (bytes_written != chunk_len))
        {
            if (fail_offset != NULL)
            {
                *fail_offset = offset;
            }
            if (fail_stage != NULL)
            {
                *fail_stage = EMMC_FS_CREATE_STAGE_WRITE;
            }
            (void)f_close(&file);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_FILE;
        }

        offset += chunk_len;
        bytes_remaining -= chunk_len;
    }

    result = f_sync(&file);
    (void)f_close(&file);
    EmmcFs_Unlock();

    if (result != FR_OK)
    {
        if (fail_offset != NULL)
        {
            *fail_offset = offset;
        }
        if (fail_stage != NULL)
        {
            *fail_stage = EMMC_FS_CREATE_STAGE_SYNC;
        }
        return EMMC_FS_ERR_READ_FILE;
    }

    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_ReadConfigMainChunk(uint32_t offset,
                                          uint8_t *buffer,
                                          uint16_t buffer_size,
                                          uint16_t *bytes_read,
                                          uint32_t *total_size)
{
    return EmmcFs_ReadFileChunk(EMMC_FS_CONFIG_MAIN_PATH,
                                offset,
                                buffer,
                                buffer_size,
                                bytes_read,
                                total_size);
}

EmmcFsStatus_t EmmcFs_ReadFileChunk(const char *filename,
                                    uint32_t offset,
                                    uint8_t *buffer,
                                    uint16_t buffer_size,
                                    uint16_t *bytes_read,
                                    uint32_t *total_size)
{
    FIL file;
    FRESULT result;
    UINT fatfs_bytes_read;
    EmmcFsStatus_t status;
    char path[EMMC_FS_FILEPATH_LEN];

    if ((filename == NULL) || (buffer == NULL) || (bytes_read == NULL) || (total_size == NULL) || (buffer_size == 0U))
    {
        return EMMC_FS_ERR_PARAM;
    }

    *bytes_read = 0U;
    *total_size = 0U;

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    memset(path, 0, sizeof(path));
    EmmcFs_BuildPath(filename, path, sizeof(path));

    result = f_open(&file, path, FA_READ);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_FILE;
    }

    *total_size = (uint32_t)f_size(&file);
    if (offset >= *total_size)
    {
        (void)f_close(&file);
        EmmcFs_Unlock();
        return EMMC_FS_OK;
    }

    result = f_lseek(&file, offset);
    if (result != FR_OK)
    {
        (void)f_close(&file);
        EmmcFs_Unlock();
        return EMMC_FS_ERR_READ_FILE;
    }

    result = f_read(&file, buffer, buffer_size, &fatfs_bytes_read);
    (void)f_close(&file);
    EmmcFs_Unlock();

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    *bytes_read = (uint16_t)fatfs_bytes_read;
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_OpenFileRead(const char *filename,
                                   EmmcFsReadHandle_t *handle,
                                   uint32_t *total_size)
{
    FRESULT result;
    EmmcFsStatus_t status;
    char path[EMMC_FS_FILEPATH_LEN];

    if ((filename == NULL) || (handle == NULL) || (total_size == NULL))
    {
        return EMMC_FS_ERR_PARAM;
    }

    memset(handle, 0, sizeof(*handle));
    *total_size = 0U;

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    memset(path, 0, sizeof(path));
    EmmcFs_BuildPath(filename, path, sizeof(path));

    result = f_open(&handle->file, path, FA_READ);
    EmmcFs_Unlock();
    if (result != FR_OK)
    {
        return EMMC_FS_ERR_OPEN_FILE;
    }

    handle->total_size = (uint32_t)f_size(&handle->file);
    handle->is_open = 1U;
    *total_size = handle->total_size;
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_ReadFileNext(EmmcFsReadHandle_t *handle,
                                   uint8_t *buffer,
                                   uint16_t buffer_size,
                                   uint16_t *bytes_read)
{
    FRESULT result;
    UINT fatfs_bytes_read;

    if ((handle == NULL) || (buffer == NULL) || (bytes_read == NULL) || (buffer_size == 0U) || (handle->is_open == 0U))
    {
        return EMMC_FS_ERR_PARAM;
    }

    *bytes_read = 0U;

    EmmcFs_Lock();
    result = f_read(&handle->file, buffer, buffer_size, &fatfs_bytes_read);
    EmmcFs_Unlock();

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    *bytes_read = (uint16_t)fatfs_bytes_read;
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_SeekFileRead(EmmcFsReadHandle_t *handle,
                                   uint32_t offset)
{
    FRESULT result;

    if ((handle == NULL) || (handle->is_open == 0U) || (offset > handle->total_size))
    {
        return EMMC_FS_ERR_PARAM;
    }

    EmmcFs_Lock();
    result = f_lseek(&handle->file, offset);
    EmmcFs_Unlock();

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_CloseFileRead(EmmcFsReadHandle_t *handle)
{
    FRESULT result;

    if (handle == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    if (handle->is_open == 0U)
    {
        return EMMC_FS_OK;
    }

    EmmcFs_Lock();
    result = f_close(&handle->file);
    EmmcFs_Unlock();

    handle->is_open = 0U;
    handle->total_size = 0U;

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_OpenFileWrite(const char *filename,
                                    EmmcFsWriteHandle_t *handle)
{
    FRESULT result;
    EmmcFsStatus_t status;
    char path[EMMC_FS_FILEPATH_LEN];

    if ((filename == NULL) || (handle == NULL))
    {
        return EMMC_FS_ERR_PARAM;
    }

    memset(handle, 0, sizeof(*handle));

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    memset(path, 0, sizeof(path));
    EmmcFs_BuildPath(filename, path, sizeof(path));

    result = f_open(&handle->file, path, FA_CREATE_ALWAYS | FA_WRITE);
    EmmcFs_Unlock();
    if (result != FR_OK)
    {
        return EMMC_FS_ERR_OPEN_FILE;
    }

    handle->bytes_written = 0U;
    handle->is_open = 1U;
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_WriteFileNext(EmmcFsWriteHandle_t *handle,
                                    const uint8_t *buffer,
                                    uint32_t buffer_size,
                                    uint32_t *bytes_written)
{
    FRESULT result;
    UINT fatfs_bytes_written;

    if ((handle == NULL) || (buffer == NULL) || (bytes_written == NULL) ||
        (buffer_size == 0U) || (handle->is_open == 0U))
    {
        return EMMC_FS_ERR_PARAM;
    }

    *bytes_written = 0U;

    EmmcFs_Lock();
    result = f_write(&handle->file, buffer, buffer_size, &fatfs_bytes_written);
    EmmcFs_Unlock();

    if ((result != FR_OK) || (fatfs_bytes_written != buffer_size))
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    handle->bytes_written += fatfs_bytes_written;
    *bytes_written = fatfs_bytes_written;
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_CloseFileWrite(EmmcFsWriteHandle_t *handle)
{
    FRESULT result;

    if (handle == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    if (handle->is_open == 0U)
    {
        return EMMC_FS_OK;
    }

    EmmcFs_Lock();
    result = f_close(&handle->file);
    EmmcFs_Unlock();

    handle->is_open = 0U;

    if (result != FR_OK)
    {
        return EMMC_FS_ERR_READ_FILE;
    }

    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_OpenRawLog(const char *filename)
{
    EmmcFsStatus_t status;

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        return EMMC_FS_ERR_PARAM;
    }

    if (s_raw_log_handle.is_open != 0U)
    {
        (void)EmmcFs_CloseFileWrite(&s_raw_log_handle);
    }

    memset(&s_raw_log_handle, 0, sizeof(s_raw_log_handle));
    status = EmmcFs_OpenFileWrite(filename, &s_raw_log_handle);
    if (status != EMMC_FS_OK)
    {
        memset(&s_raw_log_handle, 0, sizeof(s_raw_log_handle));
    }

    return status;
}

EmmcFsStatus_t EmmcFs_WriteRawLog(const uint8_t *buffer,
                                  uint32_t buffer_size,
                                  uint32_t *bytes_written)
{
    return EmmcFs_WriteFileNext(&s_raw_log_handle,
                                buffer,
                                buffer_size,
                                bytes_written);
}

EmmcFsStatus_t EmmcFs_CloseRawLog(void)
{
    return EmmcFs_CloseFileWrite(&s_raw_log_handle);
}

uint8_t EmmcFs_IsRawLogOpen(void)
{
    return s_raw_log_handle.is_open;
}

EmmcFsStatus_t EmmcFs_CountDatFiles(EmmcFsDatSummary_t *summary)
{
    DIR dir;
    FILINFO info;
    FRESULT result;
    EmmcFsStatus_t status;
    char root_path[EMMC_FS_PATH_LEN + 1U];

    if (summary == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    memset(summary, 0, sizeof(*summary));

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    (void)snprintf(root_path, sizeof(root_path), "%s%s", s_emmc_path, EMMC_FS_ROOT_SUFFIX);

    result = f_opendir(&dir, root_path);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_DIR;
    }

    for (;;)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_DIR;
        }

        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((info.fattrib & AM_DIR) == 0U)
        {
            summary->total_file_count++;

            if (EmmcFs_IsDatFile(info.fname) != 0U)
            {
                summary->dat_file_count++;
            }
        }
    }

    (void)f_closedir(&dir);
    EmmcFs_Unlock();
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_CountAllFiles(uint32_t *file_count)
{
    DIR dir;
    FILINFO info;
    FRESULT result;
    EmmcFsStatus_t status;
    char root_path[EMMC_FS_PATH_LEN + 1U];

    if (file_count == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    *file_count = 0U;

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    (void)snprintf(root_path, sizeof(root_path), "%s%s", s_emmc_path, EMMC_FS_ROOT_SUFFIX);

    result = f_opendir(&dir, root_path);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_DIR;
    }

    for (;;)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_DIR;
        }

        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((info.fattrib & AM_DIR) == 0U)
        {
            (*file_count)++;
        }
    }

    (void)f_closedir(&dir);
    EmmcFs_Unlock();
    return EMMC_FS_OK;
}


EmmcFsStatus_t EmmcFs_ListFiles(char *buffer,
                                uint32_t buffer_size,
                                uint32_t *bytes_used)
{
    DIR dir;
    FILINFO info;
    FRESULT result;
    EmmcFsStatus_t status;
    char root_path[EMMC_FS_PATH_LEN + 1U];
    uint32_t used = 0U;
    uint32_t name_len;

    if ((buffer == NULL) || (bytes_used == NULL) || (buffer_size == 0U))
    {
        return EMMC_FS_ERR_PARAM;
    }

    *bytes_used = 0U;
    buffer[0] = '\0';

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    (void)snprintf(root_path, sizeof(root_path), "%s%s", s_emmc_path, EMMC_FS_ROOT_SUFFIX);

    result = f_opendir(&dir, root_path);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_DIR;
    }

    for (;;)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_DIR;
        }

        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((info.fattrib & AM_DIR) != 0U)
        {
            continue;
        }

        name_len = (uint32_t)strlen(info.fname);
        if ((used + name_len + 1U) >= buffer_size)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            *bytes_used = used;
            return EMMC_FS_ERR_BUFFER_SMALL;
        }

        memcpy(&buffer[used], info.fname, name_len);
        used += name_len;
        buffer[used++] = '\n';
    }

    if (used > 0U)
    {
        buffer[used - 1U] = '\0';
        used -= 1U;
    }
    else
    {
        buffer[0] = '\0';
    }

    (void)f_closedir(&dir);
    EmmcFs_Unlock();
    *bytes_used = used;
    return EMMC_FS_OK;
}


EmmcFsStatus_t EmmcFs_DeleteLogFiles(uint32_t *deleted_count)
{
    DIR dir;
    FILINFO info;
    FRESULT result;
    EmmcFsStatus_t status;
    char root_path[EMMC_FS_PATH_LEN + 1U];
    char file_path[EMMC_FS_FILEPATH_LEN];

    if (deleted_count == NULL)
    {
        return EMMC_FS_ERR_PARAM;
    }

    *deleted_count = 0U;

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    (void)snprintf(root_path, sizeof(root_path), "%s%s", s_emmc_path, EMMC_FS_ROOT_SUFFIX);
    result = f_opendir(&dir, root_path);
    if (result != FR_OK)
    {
        EmmcFs_Unlock();
        return EMMC_FS_ERR_OPEN_DIR;
    }

    for (;;)
    {
        result = f_readdir(&dir, &info);
        if (result != FR_OK)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_DIR;
        }

        if (info.fname[0] == '\0')
        {
            break;
        }

        if ((info.fattrib & AM_DIR) != 0U)
        {
            continue;
        }

        if (EmmcFs_IsBinOrDatFile(info.fname) == 0U)
        {
            continue;
        }

        (void)snprintf(file_path, sizeof(file_path), "%s%s", s_emmc_path, info.fname);
        result = f_unlink(file_path);
        if (result != FR_OK)
        {
            (void)f_closedir(&dir);
            EmmcFs_Unlock();
            return EMMC_FS_ERR_READ_FILE;
        }

        (*deleted_count)++;
    }

    (void)f_closedir(&dir);
    EmmcFs_Unlock();
    return EMMC_FS_OK;
}

EmmcFsStatus_t EmmcFs_DeleteFileIfExists(const char *filename, uint32_t *deleted_count)
{
    FRESULT result;
    EmmcFsStatus_t status;
    char file_path[EMMC_FS_FILEPATH_LEN];

    if ((filename == NULL) || (deleted_count == NULL) || (filename[0] == '\0'))
    {
        return EMMC_FS_ERR_PARAM;
    }

    *deleted_count = 0U;

    EmmcFs_Lock();

    status = EmmcFs_EnsureMounted();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    (void)snprintf(file_path, sizeof(file_path), "%s%s", s_emmc_path, filename);
    result = f_unlink(file_path);
    if (result == FR_OK)
    {
        *deleted_count = 1U;
        EmmcFs_Unlock();
        return EMMC_FS_OK;
    }

    if (result == FR_NO_FILE)
    {
        EmmcFs_Unlock();
        return EMMC_FS_OK;
    }

    EmmcFs_Unlock();
    return EMMC_FS_ERR_READ_FILE;
}
