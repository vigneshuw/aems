#ifndef _LED_H_
#define _LED_H_

#include <stm32h745xx.h>
#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_tim.h>

// LED structure definition
typedef struct {
    TIM_HandleTypeDef* timer; // Pointer to the timer handle
    uint32_t redChannel;
    uint32_t greenChannel;
    uint32_t blueChannel;
} LED;

typedef enum {
    LED_STATUS_BOOTING = 0,
    LED_STATUS_ETH_INIT,
    LED_STATUS_ETH_LINK_WAIT_TCP,
    LED_STATUS_TCP_CONNECTED,
    LED_STATUS_OPENAMP_NOT_READY,
    LED_STATUS_EMMC_ERROR,
    LED_STATUS_EMMC_FORMATTING,
    LED_STATUS_DAQ_LOGGING,
    LED_STATUS_DAQ_STREAMING,
    LED_STATUS_FILE_STREAMING,
    LED_STATUS_OFFSET_CALIBRATING,
    LED_STATUS_RECOVERABLE_WARNING,
    LED_STATUS_FATAL
} LED_Status_t;

// Function prototypes
void LED_Init(LED* led, TIM_HandleTypeDef* htim, uint32_t red, uint32_t green, uint32_t blue);
void LED_ON(LED* led);
void LED_OFF(LED* led);
void LED_SetBrightness(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent);
void LED_SetStatus(LED_Status_t status);
LED_Status_t LED_GetStatus(void);
void LED_Service(LED* led);
void LED_Blink(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t delay_ms, uint8_t times);
void LED_Breathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms);
void LED_RapidBreathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms);

#endif /* _LED_H_ */
