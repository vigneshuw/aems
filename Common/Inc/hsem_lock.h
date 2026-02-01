#ifndef _HSEM_LOCK_H_
#define _HSEM_LOCK_H_

/* Exported macro ------------------------------------------------------------*/
#define LOCK_HSEM(__sem__)   while(HAL_HSEM_FastTake(__sem__) != HAL_OK) {}
#define UNLOCK_HSEM(__sem__)  HAL_HSEM_Release(__sem__, 0)

#endif
