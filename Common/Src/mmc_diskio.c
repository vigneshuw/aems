#include "ff_gen_drv.h"
#include "mmc_diskio.h"
#include "hsem_ids.h"
#include "hsem_lock.h"
#include <stdio.h>
#include "stm32h7xx_hal_mmc.h"

#define MMC_TIMEOUT SDMMC_DATATIMEOUT

#define MMC_DEFAULT_BLOCK_SIZE				512

/*
 * Depending on the usecase, the SD card initialization could be done at the
 * application level, if it is the case define the flag below to disable
 * the HAL_MMC_Init() call in the MMC_Initialize().
 */
//#define DISABLE_MMC_INIT

/*
 * when using cacheable memory region, it may be needed to maintain the cache
 * validity. Enable the define below to activate a cache maintenance at each
 * read and write operation.
 * Notice: This is applicable only for cortex M7 based platform.
 */
#define ENABLE_DMA_CACHE_MAINTENANCE		0

#define EMMC_HSEM_ID (HSEM_FS_GLOBAL_ID)

#define DISABLE_MMC_INIT

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
static volatile UINT WriteStatus = 0, ReadStatus = 0;
/* Private function prototypes -----------------------------------------------*/
static DSTATUS MMC_CheckStatus(BYTE lun);
DSTATUS MMC_initialize (BYTE);
DSTATUS MMC_status (BYTE);
DRESULT MMC_read (BYTE, BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
  DRESULT MMC_write (BYTE, const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT MMC_ioctl (BYTE, BYTE, void*);
#endif  /* _USE_IOCTL == 1 */

const Diskio_drvTypeDef  MMC_Driver =
{
	MMC_initialize,
	MMC_status,
	MMC_read,
#if  _USE_WRITE == 1
	MMC_write,
#endif /* _USE_WRITE == 1 */

#if  _USE_IOCTL == 1
	MMC_ioctl,
#endif /* _USE_IOCTL == 1 */
};

// SDMMC1 handle
extern MMC_HandleTypeDef hmmc1;

/* Private functions ---------------------------------------------------------*/
/*  Check if card is initialized/ready                          */
static DSTATUS MMC_CheckStatus(BYTE lun)
{
  DSTATUS stat = STA_NOINIT;

  LOCK_HSEM(EMMC_HSEM_ID);

  if (HAL_MMC_GetCardState(&hmmc1) == HAL_MMC_CARD_TRANSFER)
  {
    stat &= ~STA_NOINIT;
  }

  UNLOCK_HSEM(EMMC_HSEM_ID);
  return stat;
}

/**
  * @brief  Initializes a Drive
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS MMC_initialize(BYTE lun)
{
#if !defined(DISABLE_MMC_INIT)
  LOCK_HSEM(EMMC_HSEM_ID);

  if (HAL_MMC_Init(&hmmc1) == HAL_OK)
  {
    Stat = MMC_CheckStatus(lun);
  }
  else
  {
    Stat = STA_NOINIT;
  }

  UNLOCK_HSEM(EMMC_HSEM_ID);
#else
  Stat = MMC_CheckStatus(lun);
#endif

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS MMC_status(BYTE lun)
{
  return MMC_CheckStatus(lun);
}


/**
  * @brief  Reads Sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT MMC_read(BYTE lun, BYTE* buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;

  LOCK_HSEM(EMMC_HSEM_ID);

  if (HAL_MMC_ReadBlocks(&hmmc1, buff, sector, count, MMC_TIMEOUT) == HAL_OK)
  {
    /* Wait until transfer complete */
    while (HAL_MMC_GetCardState(&hmmc1) != HAL_MMC_CARD_TRANSFER)
    {
    }
    res = RES_OK;
  }
  UNLOCK_HSEM(EMMC_HSEM_ID);

  return res;
}

/**
  * @brief  Writes Sector(s)
  * @param  lun : not used
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT MMC_write(BYTE lun, const BYTE* buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;
  HAL_StatusTypeDef hal_status;

  LOCK_HSEM(EMMC_HSEM_ID);

  hal_status = HAL_MMC_WriteBlocks(&hmmc1, (uint8_t*)buff, sector, count, MMC_TIMEOUT);
  if (hal_status == HAL_OK)
  {
    /* Wait until transfer complete */
    while (HAL_MMC_GetCardState(&hmmc1) != HAL_MMC_CARD_TRANSFER)
    {
    }
    res = RES_OK;
  }

  UNLOCK_HSEM(EMMC_HSEM_ID);
  return res;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT MMC_ioctl(BYTE lun, BYTE cmd, void* buff)
{
  DRESULT res = RES_ERROR;
  HAL_MMC_CardInfoTypeDef CardInfo;

  if (Stat & STA_NOINIT) return RES_NOTRDY;

  LOCK_HSEM(EMMC_HSEM_ID);

  switch (cmd)
  {
    case CTRL_SYNC:
      res = RES_OK;
      break;

    case GET_SECTOR_COUNT:
      HAL_MMC_GetCardInfo(&hmmc1, &CardInfo);
      *(DWORD*)buff = CardInfo.LogBlockNbr;
      res = RES_OK;
      break;

    case GET_SECTOR_SIZE:
      HAL_MMC_GetCardInfo(&hmmc1, &CardInfo);
      *(WORD*)buff = CardInfo.LogBlockSize;
      res = RES_OK;
      break;

    case GET_BLOCK_SIZE:
      HAL_MMC_GetCardInfo(&hmmc1, &CardInfo);
      *(DWORD*)buff = (CardInfo.LogBlockSize / MMC_DEFAULT_BLOCK_SIZE);
      res = RES_OK;
      break;

    default:
      res = RES_PARERR;
      break;
  }

  UNLOCK_HSEM(EMMC_HSEM_ID);
  return res;
}
#endif /* _USE_IOCTL == 1 */

