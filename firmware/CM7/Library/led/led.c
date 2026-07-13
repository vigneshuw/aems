#include "led.h"

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} LedRgbPercent_t;

static volatile LED_Status_t g_led_status = LED_STATUS_BOOTING;

static uint8_t LED_IsValidStatus(LED_Status_t status)
{
    return (status <= LED_STATUS_FATAL) ? 1U : 0U;
}

static LedRgbPercent_t LED_ScaleColor(LedRgbPercent_t color, uint8_t scale_percent)
{
    LedRgbPercent_t scaled;

    scaled.red = (uint8_t)(((uint16_t)color.red * scale_percent) / 100U);
    scaled.green = (uint8_t)(((uint16_t)color.green * scale_percent) / 100U);
    scaled.blue = (uint8_t)(((uint16_t)color.blue * scale_percent) / 100U);
    return scaled;
}

static uint8_t LED_TrianglePercent(uint32_t elapsed_ms, uint16_t period_ms)
{
    uint32_t phase;
    uint32_t half_period;

    if (period_ms == 0U)
    {
        return 100U;
    }

    phase = elapsed_ms % period_ms;
    half_period = (uint32_t)period_ms / 2U;
    if (half_period == 0U)
    {
        return 100U;
    }

    if (phase < half_period)
    {
        return (uint8_t)((phase * 100U) / half_period);
    }

    return (uint8_t)(((period_ms - phase) * 100U) / half_period);
}

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

void LED_SetStatus(LED_Status_t status)
{
    if (LED_IsValidStatus(status) != 0U)
    {
        g_led_status = status;
    }
}

LED_Status_t LED_GetStatus(void)
{
    return g_led_status;
}

void LED_Service(LED* led)
{
    static LED_Status_t last_status = LED_STATUS_BOOTING;
    static uint32_t status_start_ms = 0U;
    static LedRgbPercent_t last_color = { 255U, 255U, 255U };
    LED_Status_t status;
    LedRgbPercent_t color = { 0U, 0U, 0U };
    LedRgbPercent_t target;
    uint32_t now_ms;
    uint32_t elapsed_ms;
    uint8_t on_phase;
    uint8_t pulse;

    if (led == NULL)
    {
        return;
    }

    status = g_led_status;
    now_ms = HAL_GetTick();
    if (status != last_status)
    {
        last_status = status;
        status_start_ms = now_ms;
    }

    elapsed_ms = now_ms - status_start_ms;

    switch (status)
    {
        case LED_STATUS_BOOTING:
            color = (LedRgbPercent_t){ 35U, 35U, 35U };
            break;

        case LED_STATUS_ETH_INIT:
            on_phase = (((elapsed_ms / 500U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 0U, 0U, 60U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_ETH_LINK_WAIT_TCP:
            color = (LedRgbPercent_t){ 0U, 0U, 60U };
            break;

        case LED_STATUS_TCP_CONNECTED:
            color = (LedRgbPercent_t){ 0U, 50U, 0U };
            break;

        case LED_STATUS_OPENAMP_NOT_READY:
            on_phase = (((elapsed_ms / 500U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 70U, 60U, 0U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_EMMC_ERROR:
            on_phase = (((elapsed_ms / 500U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 80U, 0U, 0U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_EMMC_FORMATTING:
            on_phase = (((elapsed_ms / 500U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 55U, 0U, 60U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_DAQ_LOGGING:
            target = (LedRgbPercent_t){ 0U, 55U, 55U };
            pulse = (uint8_t)(20U + ((((uint16_t)LED_TrianglePercent(elapsed_ms, 1200U)) * 80U) / 100U));
            color = LED_ScaleColor(target, pulse);
            break;

        case LED_STATUS_DAQ_STREAMING:
            on_phase = (((elapsed_ms / 125U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 0U, 60U, 0U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_FILE_STREAMING:
            on_phase = (((elapsed_ms / 125U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 0U, 55U, 55U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_OFFSET_CALIBRATING:
            color = (LedRgbPercent_t){ 70U, 60U, 0U };
            break;

        case LED_STATUS_RECOVERABLE_WARNING:
            on_phase = (((elapsed_ms / 300U) & 1U) == 0U) ? 1U : 0U;
            color = (on_phase != 0U) ? (LedRgbPercent_t){ 80U, 28U, 0U } : (LedRgbPercent_t){ 0U, 0U, 0U };
            break;

        case LED_STATUS_FATAL:
        default:
            color = (LedRgbPercent_t){ 100U, 0U, 0U };
            break;
    }

    if ((color.red != last_color.red) ||
        (color.green != last_color.green) ||
        (color.blue != last_color.blue))
    {
        LED_SetBrightness(led, color.red, color.green, color.blue);
        last_color = color;
    }
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
