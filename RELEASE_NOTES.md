# AEMS v1.0.0 — First public open-hardware release

First public release of the **Autonomous Energy Monitoring System (AEMS)**: an
open-source, low-cost, modular platform for high-resolution energy monitoring of
manufacturing equipment. This release contains the complete hardware design, the
dual-core embedded firmware, and the host-side software stack, accompanying the
paper *"An Open-Source, Autonomous Platform for High-Resolution Energy Monitoring
in Manufacturing."*

## What's included

**Hardware** (`hardware/`) — Complete KiCad 7 design for the AEMS v2 board:
hierarchical schematics, PCB layout, custom and third-party part libraries,
LTSpice front-end simulations, stackup/impedance design summaries, an interactive
BOM (`ibom.html`), and ready-to-fabricate PCBWay outputs (Gerbers, drill, BOM,
pick-and-place).

**Firmware** (`firmware/`) — STM32H745 dual-core firmware:
- **CM4** — deterministic acquisition (ADS131M08), DAQ state machine, on-board
  eMMC logging via FatFs, and OpenAMP remote service.
- **CM7** — Ethernet/TCP host link, FreeRTOS/LwIP, OpenAMP master, command and
  response handling, file/DAQ streaming.

**Host software** (`firmware/BoardInterfaceLibrary/`) — Python `board_interface`
library for single- and multi-board control, DAQ stream/log decoding, examples,
a Raspberry Pi daemon (`aems-boardd` / `aemsctl`), and an optional AWS IoT cloud
agent.

## Hardware highlights

- **ADS131M08** 8-channel, 24-bit, simultaneously sampling delta-sigma ADC front end
- **STM32H745** dual-core (Cortex-M7 + Cortex-M4) MCU
- Isolated power rails and impedance-controlled multilayer PCB
- MOV-based surge protection on the measurement inputs
- On-board eMMC for autonomous logging; PoE-capable Ethernet
- Current build acquires 6 channels (three voltage, three current)

## Validation

The platform was validated on a three-axis CNC machining center, where it resolves
spindle, feed-drive, rapid-traverse, and material-removal energy states and detects
feed-rate changes as small as 50 mm/min.

## Getting started

- System overview and setup: [`README.md`](README.md)
- Firmware architecture: [`firmware/README.md`](firmware/README.md) and `firmware/docs/Software/README.md`
- Host library: [`firmware/BoardInterfaceLibrary/README.md`](firmware/BoardInterfaceLibrary/README.md)
- Hardware / fabrication: [`hardware/README.md`](hardware/README.md)
- Raspberry Pi server & cloud: `firmware/BoardInterfaceLibrary/docs/`

## How to cite

If you use AEMS, please cite the paper and this archived release. See
[`CITATION.cff`](CITATION.cff).

- Paper: Selvaraj, V., Nagaraj, A., Zhang, S., Sadeghian, S., Min, S.,
  *An Open-Source, Autonomous Platform for High-Resolution Energy Monitoring in
  Manufacturing* (2026).
- Software archive: Zenodo DOI `10.5281/zenodo.XXXXXXX` *(add after archiving)*

## License

Released under a per-material split (full texts in [`LICENSES/`](LICENSES/)):

- Hardware design files — **CERN-OHL-P-2.0**
- Firmware and host software — **MIT**
- Documentation and figures — **CC-BY-4.0**

Vendor/third-party components (ST HAL/CMSIS, FreeRTOS, OpenAMP, FatFs, BlueNRG-2,
downloaded KiCad libraries) retain their own licenses.

## Known limitations

- **6-channel operation only.** The current board/firmware build and host workflows
  assume a channel mask of `0x3F` (channels 0–5). Do not request a wider mask until
  the firmware explicitly supports it.
- **Input voltage topology.** The present hardware is validated for its rated
  direct-measurement input. Higher line-to-line service voltages (e.g. 480 V L-L)
  exceed the on-board MOV continuous ratings and require an external potential
  transformer (tracked under `hardware/FutureUpdates/`).
- **Stream/control framing (temporary).** The host library uses a temporary
  heuristic to separate command `12` ACK traffic from command `13` DAQ stream
  payload on the same TCP connection. A firmware/protocol change to explicitly
  frame stream traffic is the planned long-term fix.

## Release artifacts

In addition to the source archives attached by GitHub, this release may include:

- **Fabrication package** — zipped `hardware/Manufacturing/PCBWay/` (Gerbers, drill,
  BOM, pick-and-place) plus the interactive BOM and a schematic PDF
- **Compiled firmware** — CM4 and CM7 binaries (`.elf` / `.bin` / `.hex`)
- **Python package** — built wheel/sdist of `BoardInterfaceLibrary`
