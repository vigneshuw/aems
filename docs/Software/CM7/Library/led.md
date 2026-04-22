# CM7 Library: led

## Scope

`led` is a small CM7 utility library for controlling the onboard RGB LED using a timer PWM peripheral.

It is not part of the DAQ or TCP data path, but it is useful for visual bring-up and runtime indication.

## Source files

| File | Role |
|---|---|
| `CM7/Library/led/led.h` | public LED structure and API |
| `CM7/Library/led/led.c` | implementation |

## Data type

### `LED`

```c
typedef struct {
    TIM_HandleTypeDef* timer;
    uint32_t redChannel;
    uint32_t greenChannel;
    uint32_t blueChannel;
} LED;
```

Fields:
- `timer`: PWM timer instance
- `redChannel`: PWM channel used for red LED
- `greenChannel`: PWM channel used for green LED
- `blueChannel`: PWM channel used for blue LED

## Public API reference

### `void LED_Init(LED* led, TIM_HandleTypeDef* htim, uint32_t red, uint32_t green, uint32_t blue)`

Initializes the LED object with timer/channel assignments.

Arguments:
- `led`: destination LED object
- `htim`: timer handle
- `red`: timer channel for red
- `green`: timer channel for green
- `blue`: timer channel for blue

### `void LED_ON(LED* led)`

Starts PWM output on all configured channels.

### `void LED_OFF(LED* led)`

Stops PWM output.

### `void LED_SetBrightness(LED* led, uint8_t redPercent, uint8_t greenPercent, uint8_t bluePercent)`

Sets channel brightness percentages.

Arguments:
- `redPercent`: 0..100
- `greenPercent`: 0..100
- `bluePercent`: 0..100

### `void LED_Blink(...)`

Blocking blink helper.

Arguments:
- `redPercent`, `greenPercent`, `bluePercent`: brightness percentages
- `delay_ms`: on/off delay
- `times`: blink count

### `void LED_Breathe(...)`

Blocking breathe effect helper.

Arguments:
- `redPercent`, `greenPercent`, `bluePercent`: target brightness
- `duration_ms`: effect duration

### `void LED_RapidBreathe(...)`

Variant of breathe effect with faster transition timing.

## Current usage in firmware

In `CM7/Core/Src/main.c` the current boot sequence does:

- initialize TIM1
- call `LED_Init(...)`
- call `LED_ON(...)`
- set the LED to a green-ish brightness with `LED_SetBrightness(&rgbLed, 0, 40, 0)`

That makes the LED library primarily a bring-up/status helper today.
