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

// Function prototypes
void LED_Init(LED* led, TIM_HandleTypeDef* htim, uint32_t red, uint32_t green, uint32_t blue);
void LED_ON(LED* led);
void LED_OFF(LED* led);
void LED_SetBrightness(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent);
void LED_Blink(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t delay_ms, uint8_t times);
void LED_Breathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms);
void LED_RapidBreathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms);

#endif /* _LED_H_ */
