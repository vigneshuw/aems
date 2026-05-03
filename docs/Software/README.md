# AEMS Firmware Software Documentation

## Purpose

This firmware runs the Adaptive Energy Monitoring System (AEMS) board built around an STM32H745 dual-core MCU. The board acquires multi-channel electrical measurements through an ADS131M08 ADC, stores captured data to eMMC, and exchanges commands and data with a host computer over Ethernet/TCP.

The design deliberately splits responsibility across both cores:

- **CM4** owns deterministic acquisition, buffering, and eMMC file operations.
- **CM7** owns networking, TCP command handling, host-facing streaming, and supervisory control.

That split is the core architectural decision in this codebase. The firmware is trying to achieve three things at the same time:

1. **capture data deterministically** from the analog front end
2. **persist data safely** to eMMC in a format that can be streamed or decoded later
3. **expose the board as a remotely controlled instrument** through a simple TCP protocol

## Current firmware status

The current codebase implements the following board-level capabilities:

- CM7 TCP client connection to a host Python server
- CM7 command parser and response framing
- CM7 file and DAQ streaming over TCP
- CM4 OpenAMP remote service for filesystem and DAQ control
- CM4 ADS131M08 acquisition pipeline using SPI + DMA + cooperative superloop
- CM4 eMMC filesystem wrapper using FatFs on SDMMC1/MMC
- CM4 eMMC first-mount/format recovery with command `99` diagnostics
- CM4 SDMMC1 ClockDiv boot policy: slower ClockDiv during mount/format, runtime ClockDiv after success
- DAQ logging to eMMC files
- DAQ live streaming from CM4 to CM7 via shared memory
- file count, file list, file size, file delete, and file stream operations
- persistent ADC offset calibration commands `97` / `98`

Known current constraints:

- the host tooling and most workflows currently assume **channel mask `0x3F`**
- command `11` eMMC DAQ log files and command `13` live DAQ streams use **different binary layouts**
- CM7/host-side DAQ stop during command `13` still relies on a temporary protocol heuristic and should be cleaned up by a future firmware framing update
- command `99` is the primary bring-up diagnostic; it now includes raw FatFs mount/format results for blank or marginal eMMC parts

## Hardware and software interplay

The firmware is tightly coupled to the board hardware. The main hardware/software boundaries are:

| Hardware block | Firmware owner | Notes |
|---|---|---|
| ADS131M08 ADC | CM4 | Sample clocking, SPI4 DMA reads, DRDY interrupt handling, per-sample packing |
| eMMC on SDMMC1 | CM4 | FatFs mount/format, ClockDiv policy, file I/O, DAQ raw log writes |
| Ethernet PHY / LwIP | CM7 | Host connectivity, TCP link, command transport |
| Shared SRAM region | CM4 + CM7 | File and DAQ streaming handoff between cores |
| OpenAMP / RPMsg | CM4 + CM7 | Control plane between the two cores |
| HSEM boot synchronization | CM4 + CM7 | Dual-core startup coordination |
| RGB LED | CM7 | Basic board-visible status indication |

### System data flow

```plantuml
@startuml
skinparam monochrome true
skinparam shadowing false
actor Host
rectangle CM7 {
  component "TCP client +\ncommand controller" as TCP
  component "OpenAMP master" as OA7
}
rectangle CM4 {
  component "OpenAMP remote" as OA4
  component "DAQ engine" as DAQ
  component "eMMC / FatFs" as FS
}
component "ADS131M08" as ADC
component "Shared SRAM" as SHM

Host --> TCP : TCP commands / stream sink
TCP --> OA7 : translate host requests
OA7 --> OA4 : RPMsg command
OA4 --> DAQ : start/stop/status
OA4 --> FS : list/read/write/delete
ADC --> DAQ : DRDY + SPI DMA samples
DAQ --> FS : raw DAQ log writes
FS --> OA4 : file chunks
DAQ --> SHM : DAQ stream blocks
FS --> SHM : file stream blocks
SHM --> TCP : zero-copy / pointer-style send
TCP --> Host : fixed responses + streamed bytes
@enduml
```

## Dual-core role split

### CM4 summary

CM4 is the real-time data plane. It:

- initializes and mounts eMMC
- configures and services the ADS131M08
- runs the DAQ state machine
- aggregates acquired samples into write blocks
- writes DAQ data to eMMC
- provides filesystem and DAQ services to CM7 through OpenAMP

See: [CM4 documentation](CM4/README.md)

### CM7 summary

CM7 is the supervisory and transport plane. It:

- boots FreeRTOS and LwIP
- opens a TCP connection to the host
- receives host commands and maps them to board actions
- calls CM4 OpenAMP services
- streams file and DAQ data over TCP
- frames host-visible responses

See: [CM7 documentation](CM7/README.md)

## Source tree orientation

| Area | Purpose |
|---|---|
| `CM4/Core/Src` | CM4 boot, superloop, OpenAMP remote endpoint |
| `CM4/Library/ads131m08` | ADC driver, DAQ engine, DAQ state machine |
| `CM4/Library/emmc_fs` | FatFs wrapper and raw DAQ log file API |
| `CM7/Core/Src` | CM7 boot, FreeRTOS tasks, OpenAMP master |
| `CM7/Library/eth` | TCP client used by the host-facing command path |
| `CM7/Library/led` | RGB LED helper |
| `Common/Inc` | shared DAQ structures, HSEM IDs, shared-memory definitions |
| `BoardInterfaceLibrary` | Python host library and examples |

## Documentation map

### Core-level documents

- [CM4 / Overview](CM4/README.md)
- [CM7 / Overview](CM7/README.md)

### Library-level documents

#### CM4
- [CM4 Library Index](CM4/Library/README.md)
- [CM4 ads131m08 and DAQ engine](CM4/Library/ads131m08.md)
- [CM4 eMMC filesystem wrapper](CM4/Library/emmc_fs.md)
- [CM4 OpenAMP remote service](CM4/Library/openamp_remote.md)

#### CM7
- [CM7 Library Index](CM7/Library/README.md)
- [CM7 TCP client library](CM7/Library/tcpclient.md)
- [CM7 LED helper library](CM7/Library/led.md)
- [CM7 OpenAMP master service](CM7/Library/openamp_master.md)

## Boot and runtime overview

### Boot sequence

1. CM7 powers up first, configures clocks/cache/MPU, and releases CM4 through HSEM.
2. CM4 wakes, initializes SPI4, SDMMC1, DAQ context, sets SDMMC1 to mount ClockDiv `10`, and mounts/formats the eMMC filesystem.
3. CM4 brings up the OpenAMP remote endpoint.
4. CM7 starts FreeRTOS, LwIP, TCP client, and OpenAMP master.
5. The board is then ready to accept host commands.

After a successful eMMC mount or first-boot format, CM4 switches SDMMC1 back to runtime ClockDiv `8` and then loads persisted DAQ offset calibration from `ocal.cfg` when present.

### Command path overview

```plantuml
@startuml
skinparam monochrome true
skinparam shadowing false
actor Host
participant "CM7 TcpClient" as TCP
participant "CM7 ControllerTask" as CTRL
participant "CM7 OpenAmpFs" as OA7
participant "CM4 OpenAmpFs" as OA4
participant "CM4 DAQ / FS" as CM4

Host -> TCP : TCP payload
TCP -> CTRL : enqueue decoded command
CTRL -> OA7 : service request
OA7 -> OA4 : RPMsg request
OA4 -> CM4 : DAQ or filesystem operation
CM4 --> OA4 : result / data metadata
OA4 --> OA7 : RPMsg reply
CTRL -> TCP : fixed reply or start stream
TCP --> Host : response bytes
@enduml
```

## Data formats that matter

Two binary DAQ formats exist today:

### 1. eMMC log file format from command `11`

- selected channels only
- current host physical-value tooling assumes channels `0..5` (`channel_mask = 0x3F`)
- firmware can validate an 8-bit channel mask, but widening the mask changes the file stride and host decoder assumptions
- each channel stored as signed 32-bit little-endian
- **24 bytes per sample** for current board assumptions

### 2. live DAQ stream format from command `13`

- `response` (`uint16_le`)
- `crc` (`uint16_le`)
- `channel[0..7]` as eight signed 32-bit little-endian values
- **36 bytes per frame**

This distinction is important in both firmware and host tooling.

## Bring-up diagnostics

Command `99` returns the health snapshot used during board bring-up and eMMC troubleshooting.

Important storage fields:

| Field | Meaning |
|---|---|
| `remote_mount_status` | CM4 public `EmmcFs_MountOrFormat()` result; `0` means storage is usable |
| `emmc_mount_stage` | last mount lifecycle stage: link, first mount, mkfs, or post-format mount |
| `emmc_mount_fresult` | FatFs result from the first `f_mount()` |
| `emmc_mkfs_fresult` | FatFs result from `f_mkfs()` |
| `emmc_post_mount_fresult` | FatFs result from the mount after formatting |

A blank eMMC can validly report `remote_mount_status=0`, `emmc_mount_fresult=13`, `emmc_mkfs_fresult=0`, and `emmc_post_mount_fresult=0`. That means the first mount found no filesystem, firmware created one, and the post-format mount succeeded.

## RGB LED Board Status

CM7 owns the onboard RGB LED and updates it from `TelemetryTask`. The LED service is non-blocking: the telemetry task selects a board status and calls `LED_Service(&rgbLed)` every 25 ms. Blink and pulse effects are generated from `HAL_GetTick()` and do not block TCP, OpenAMP, or DAQ work.

The LED is intended as the first field diagnostic. An end user should be able to look at the board and know whether the issue is network, host connection, CM4/OpenAMP, eMMC, active acquisition, or a fatal fault.

### Status Colors

| Priority | LED | Meaning | Typical trigger |
|---:|---|---|---|
| 1 | Red solid | Fatal firmware error | `Error_Handler()` |
| 2 | Red slow blink | eMMC/storage error | CM4 mount status is nonzero after command `99` diagnostics are available |
| 3 | Yellow blink | CM4/OpenAMP not ready | OpenAMP init failed or latest heartbeat/probe showed CM4 service not healthy |
| 4 | Yellow solid | Offset calibration active | command `98` is running |
| 5 | Cyan pulse | DAQ logging to eMMC | command `11` succeeded and logging is active |
| 6 | Green fast blink | Live DAQ streaming | command `13` stream is active |
| 7 | Cyan fast blink | File stream active | command `8` stream is active |
| 8 | Orange blink | Recoverable warning | command/stream failure, dropped samples reported by command `10`/`110`, or retry condition |
| 9 | Green solid | TCP connected and idle | board is connected to host and no higher-priority activity/error is active |
| 10 | Blue solid | Ethernet link up, TCP not connected | PHY/link is up but Python host server is not connected |
| 11 | Blue slow blink | Ethernet/PHY initialization or no link | PHY reset/link bring-up, cable/switch missing, or link not stable |
| 12 | White solid | Booting | LED initialized before runtime status is known |

### How to Interpret the LED

- **White solid** should be brief. If it stays white, CM7 likely did not reach the FreeRTOS telemetry loop.
- **Blue slow blink** points at physical Ethernet: cable, switch, PHY reset/init, or link negotiation.
- **Blue solid** means Ethernet is good but the host TCP server is not connected. Start the Python tool and verify the configured host IP is reachable.
- **Green solid** is the normal idle state after the board connects to the host.
- **Yellow blink** means CM7 is alive but cannot confirm CM4/OpenAMP health. Use command `99`.
- **Red slow blink** means eMMC is not usable. Use command `99` and check `remote_mount_status`, `emmc_mount_stage`, and FatFs `FRESULT` fields.
- **Purple slow blink** means the eMMC filesystem creation path is active. This can happen on a new blank eMMC.
- **Cyan pulse / green fast blink / cyan fast blink** identify active data movement: eMMC DAQ logging, live DAQ streaming, or file streaming.
- **Orange blink** is intentionally nonfatal. It tells the user to query command `10`, `110`, or `99` for detail.
- **Red solid** means fatal firmware error and requires reset/debug.

See also: [CM7 LED helper library](CM7/Library/led.md)

## Recommended reading order

1. read this page first
2. read [CM4 / Overview](CM4/README.md)
3. read [CM7 / Overview](CM7/README.md)
4. use the library pages for function-level interface details
