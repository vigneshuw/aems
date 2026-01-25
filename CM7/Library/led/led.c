#include "led.h"


// Initialize the LED structure
void LED_Init(LED* led, TIM_HandleTypeDef* htim, uint32_t red, uint32_t green, uint32_t blue) {
    led->timer = htim;
    led->redChannel = red;
    led->greenChannel = green;
    led->blueChannel = blue;
}


void LED_ON(LED* led) {
    HAL_TIM_PWM_Start(led->timer, led->redChannel);
    HAL_TIM_PWM_Start(led->timer, led->greenChannel);
    HAL_TIM_PWM_Start(led->timer, led->blueChannel);
}


void LED_OFF(LED* led) {
    HAL_TIM_PWM_Stop(led->timer, led->redChannel);
    HAL_TIM_PWM_Stop(led->timer, led->greenChannel);
    HAL_TIM_PWM_Stop(led->timer, led->blueChannel);
}

void LED_SetBrightness(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent) {
    // Ensure percentages are within 0-100 range
    if (redPercent > 100) redPercent = 100;
    if (greenPercent > 100) greenPercent = 100;
    if (bluePercent > 100) bluePercent = 100;

    // Get the maximum PWM value from the timer's auto-reload register (ARR)
    uint16_t PWM_MAX = __HAL_TIM_GET_AUTORELOAD(led->timer);

    // Scale percentages to PWM compare values
    uint16_t redValue = (redPercent * PWM_MAX) / 100;
    uint16_t greenValue = (greenPercent * PWM_MAX) / 100;
    uint16_t blueValue = (bluePercent * PWM_MAX) / 100;

    // Set the compare values for each channel
    __HAL_TIM_SET_COMPARE(led->timer, led->redChannel, redValue);
    __HAL_TIM_SET_COMPARE(led->timer, led->greenChannel, greenValue);
    __HAL_TIM_SET_COMPARE(led->timer, led->blueChannel, blueValue);
}


void LED_Blink(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t delay_ms, uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        LED_SetBrightness(led, redPercent, greenPercent, bluePercent);
        HAL_Delay(delay_ms);
        LED_SetBrightness(led, 0, 0, 0);
        HAL_Delay(delay_ms);
    }
}


void LED_Breathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms) {
    const uint16_t total_steps = 100; // Total steps for fade-in and fade-out
    const uint16_t step_delay = duration_ms / (2 * total_steps); // Calculate step delay based on total duration

    // Fade-in
    for (uint8_t i = 0; i <= total_steps; i++) {
        uint8_t r = (redPercent * i) / total_steps;
        uint8_t g = (greenPercent * i) / total_steps;
        uint8_t b = (bluePercent * i) / total_steps;
        LED_SetBrightness(led, r, g, b);
        HAL_Delay(step_delay);
    }

    // Fade-out
    for (uint8_t i = total_steps; i > 0; i--) {
        uint8_t r = (redPercent * i) / total_steps;
        uint8_t g = (greenPercent * i) / total_steps;
        uint8_t b = (bluePercent * i) / total_steps;
        LED_SetBrightness(led, r, g, b);
        HAL_Delay(step_delay);
    }
}


void LED_RapidBreathe(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent, uint16_t duration_ms) {
    const uint16_t rapid_duration = 2000; // Rapid breathing cycle (2 seconds)
    const uint16_t total_steps = 50; // Total steps for fade-in and fade-out
    const uint16_t step_delay = rapid_duration / (2 * total_steps); // Step delay for rapid breathing

    uint16_t elapsed_time = 0;

    while (elapsed_time < duration_ms) {
        // Fade-in
        for (uint8_t i = 0; i <= total_steps; i++) {
            uint8_t r = (redPercent * i) / total_steps;
            uint8_t g = (greenPercent * i) / total_steps;
            uint8_t b = (bluePercent * i) / total_steps;
            LED_SetBrightness(led, r, g, b);
            HAL_Delay(step_delay);
        }

        // Fade-out
        for (uint8_t i = total_steps; i > 0; i--) {
            uint8_t r = (redPercent * i) / total_steps;
            uint8_t g = (greenPercent * i) / total_steps;
            uint8_t b = (bluePercent * i) / total_steps;
            LED_SetBrightness(led, r, g, b);
            HAL_Delay(step_delay);
        }

        elapsed_time += rapid_duration; // Track total elapsed time
    }
}
