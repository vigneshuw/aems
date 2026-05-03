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

## Public API Reference

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

### `void LED_SetStatus(LED_Status_t status)`

Sets the desired board-level status. This function does not directly blink or delay. The physical LED is updated later by `LED_Service()`.

Arguments:
- `status`: one of the `LED_STATUS_*` values.

### `LED_Status_t LED_GetStatus(void)`

Returns the current requested LED status.

### `void LED_Service(LED* led)`

Non-blocking LED state-machine service. It should be called periodically by firmware.

Current firmware calls it from `TelemetryTask` every 25 ms:

```c
LED_SetStatus(Telemetry_SelectLedStatus());
LED_Service(&rgbLed);
```

Blinking and pulse effects use `HAL_GetTick()` and compare timestamps. This means the LED status system does not block FreeRTOS, TCP, OpenAMP, or DAQ handling.

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

In `CM7/Core/Src/main.c` the boot sequence does:

- initialize TIM1
- call `LED_Init(...)`
- call `LED_ON(...)`
- set the initial LED state to `LED_STATUS_BOOTING`

After FreeRTOS starts, `TelemetryTask` owns the runtime LED state. It periodically evaluates Ethernet, TCP, OpenAMP, eMMC, DAQ, stream, calibration, and warning state.

## Board Status Map

| LED status enum | LED behavior | User meaning |
|---|---|---|
| `LED_STATUS_BOOTING` | White solid | Firmware started; initialization is in progress |
| `LED_STATUS_ETH_INIT` | Blue slow blink | Ethernet PHY/MAC link is not ready |
| `LED_STATUS_ETH_LINK_WAIT_TCP` | Blue solid | Ethernet link is up, waiting for host TCP server |
| `LED_STATUS_TCP_CONNECTED` | Green solid | Host TCP connection is active and board is idle |
| `LED_STATUS_OPENAMP_NOT_READY` | Yellow blink | CM7 is alive, but CM4/OpenAMP service is not healthy |
| `LED_STATUS_EMMC_ERROR` | Red slow blink | eMMC mount/storage is not usable |
| `LED_STATUS_EMMC_FORMATTING` | Purple slow blink | Blank eMMC filesystem creation is in progress |
| `LED_STATUS_DAQ_LOGGING` | Cyan pulse | DAQ logging to eMMC is active |
| `LED_STATUS_DAQ_STREAMING` | Green fast blink | Live DAQ TCP stream is active |
| `LED_STATUS_FILE_STREAMING` | Cyan fast blink | eMMC file stream to host is active |
| `LED_STATUS_OFFSET_CALIBRATING` | Yellow solid | Offset calibration command `98` is running |
| `LED_STATUS_RECOVERABLE_WARNING` | Orange blink | Nonfatal warning; query command `10`, `110`, or `99` |
| `LED_STATUS_FATAL` | Red solid | Firmware entered `Error_Handler()` |

## Runtime Priority

The telemetry task selects the highest-priority status. Error and health states override normal connected/idle states.

Priority order:

1. Ethernet link not ready
2. Ethernet link up but TCP not connected
3. OpenAMP not ready
4. eMMC error or eMMC formatting
5. offset calibration
6. DAQ logging
7. live DAQ stream
8. file stream
9. recoverable warning
10. TCP connected and idle

Fatal error bypasses telemetry and is forced directly by `Error_Handler()`.

## Notes

- Prefer `LED_SetStatus()` + `LED_Service()` for runtime indication.
- Avoid `LED_Blink()`, `LED_Breathe()`, and `LED_RapidBreathe()` from FreeRTOS tasks that must remain responsive because those helpers are blocking.
- Brightness values are intentionally below 100% for most runtime states to avoid an overly bright board indicator.
