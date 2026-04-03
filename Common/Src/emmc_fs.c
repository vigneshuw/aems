#include "emmc_fs.h"

#include "ff.h"
#include "ff_gen_drv.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include "mmc_diskio.h"

#include <stdio.h>
#include <string.h>

#define EMMC_FS_PATH_LEN            4U
#define EMMC_FS_WORKBUF_LEN         _MAX_SS
#define EMMC_FS_ROOT_SUFFIX         "/"
#define EMMC_FS_API_HSEM_ID         HSEM_FS_API_ID

static FATFS s_emmc_fs;
static char s_emmc_path[EMMC_FS_PATH_LEN];
static uint8_t s_driver_linked;
static uint8_t s_fs_mounted;
static uint8_t s_work_buffer[EMMC_FS_WORKBUF_LEN];

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

    status = EmmcFs_EnsureLinked();
    if (status != EMMC_FS_OK)
    {
        EmmcFs_Unlock();
        return status;
    }

    if (s_fs_mounted == 0U)
    {
        result = f_mount(&s_emmc_fs, s_emmc_path, 1U);
        if (result != FR_OK)
        {
            EmmcFs_Unlock();
            return EMMC_FS_ERR_MOUNT;
        }

        s_fs_mounted = 1U;
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
