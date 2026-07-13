# AEMS — Autonomous Energy Monitoring System

An open-source, low-cost, modular platform for high-resolution energy monitoring of manufacturing equipment. AEMS acquires three-phase voltage and current through a front end and a 24-bit, simultaneously sampling ADC, managed by a dual-core architecture that separates deterministic acquisition and on-board logging from host communication and control. A host, edge-gateway, and optional cloud software stack enable autonomous, long-duration acquisition independent of a continuously connected host, combining research-grade fidelity with industrial deployability for Industry 4.0 environments.

This repository accompanies the paper (preprint):

> **An Open-Source, Autonomous Platform for High-Resolution Energy Monitoring in Manufacturing**
> Vignesh Selvaraj, Aditya Nagaraj, Shengyuan Zhang, Sina Sadeghian, Sangkee Min
> Department of Mechanical Engineering, University of Wisconsin–Madison.

If you use this hardware, firmware, or software in your work, please see
[Citation](#citation) and [`CITATION.cff`](CITATION.cff).

---

## Why AEMS

Long-term, robust monitoring of machine energy consumption is difficult: commercial power
meters capable of capturing transient events are expensive, closed, and impractical to deploy
across an entire facility. AEMS was designed to make high-fidelity energy monitoring
**affordable, customizable, and reproducible**, so that small and medium-sized manufacturers
can instrument their equipment and improve data availability for Industry 4.0 analytics.

Design goals:

- **High-rate, simultaneous multi-channel acquisition** of voltage and current signals.
- **Autonomous operation** — capture and log on the board even when a host or network is unavailable, then stream or transfer data later.
- **Modular and open** — hardware, firmware, and host software are all open and independently reusable.
- **Deployable at scale** — many boards can share a network and be controlled from a single host or from an edge computer such as Raspberry Pi.

---

## System overview

AEMS is organized into three layers, each of which lives in this repository:

| Layer | Location | Role |
|---|---|---|
| **Hardware** | [`hardware/`](hardware/) | Custom AEMS v2 DAQ board — KiCad design, ADS131M08 analog front end, surge protection, impedance-controlled PCB. |
| **Firmware** | [`firmware/`](firmware/) | STM32H745 dual-core firmware. **CM4** owns deterministic acquisition + eMMC storage; **CM7** owns Ethernet/TCP + host command/control over OpenAMP. |
| **Host software** | [`firmware/BoardInterfaceLibrary/`](firmware/BoardInterfaceLibrary/) | Python `board_interface` library, examples, Raspberry Pi daemon (`aemsctl`), and an optional AWS IoT cloud agent. |

```
Machine mains ──▶ AEMS board (ADS131M08 front end)
                        │
                   STM32H745
              ┌─────────┴─────────┐
        CM4 (DAQ + eMMC)   CM7 (Ethernet/TCP)
                        │
                Host (PC / Raspberry Pi)
             board_interface  ·  aemsctl daemon
```

Data flows from the analog front end through the ADS131M08 acquisition path on CM4, is logged
to on-board eMMC and/or streamed over TCP to a host, and can optionally be forwarded to the
cloud for remote monitoring and transfer.

---

## Repository layout

```
aems/
├── hardware/                     KiCad project for the AEMS v2 board
│   ├── *.kicad_sch / *.kicad_pcb Schematics and PCB layout
│   ├── CustomLibs/               Project symbol/footprint libraries
│   ├── LTSpiceSimulation/        Analog front-end simulations (CT op-amp, etc.)
│   ├── DesignSummary/            Stackup, impedance control, interactive BOM (ibom.html)
│   ├── Manufacturing/            PCBWay fabrication outputs
│   └── FutureUpdates/            Planned revisions (e.g. front-end isolation)
│
└── firmware/                     STM32H745 dual-core firmware + host tooling
    ├── CM4/                      Cortex-M4: DAQ, ADS131M08, eMMC/FatFs, OpenAMP
    ├── CM7/                      Cortex-M7: Ethernet/TCP, FreeRTOS, OpenAMP master
    ├── Common/                   Shared cross-core headers/data structures
    ├── BoardInterfaceLibrary/    Python host library, CLI, Raspberry Pi daemon, cloud agent
    ├── docs/Software/            Firmware architecture documentation
    └── UnitTests/                Bench connectivity/test scripts
```

Detailed documentation already lives with each subtree:

- **Firmware architecture** — [`firmware/README.md`](firmware/README.md) and
  [`firmware/docs/Software/README.md`](firmware/docs/Software/README.md)
- **Host Python library** — [`firmware/BoardInterfaceLibrary/README.md`](firmware/BoardInterfaceLibrary/README.md)
- **Raspberry Pi server & cloud** —
  [`firmware/BoardInterfaceLibrary/docs/raspberry_pi_server.md`](firmware/BoardInterfaceLibrary/docs/raspberry_pi_server.md),
  [`firmware/BoardInterfaceLibrary/docs/cloud_autonomy.md`](firmware/BoardInterfaceLibrary/docs/cloud_autonomy.md)
- **Hardware** — [`hardware/README.md`](hardware/README.md)

---

## Hardware at a glance

The AEMS v2 board is built around:

- **ADS131M08** 8-channel, 24-bit simultaneous-sampling delta-sigma ADC as the analog front end.
- **STM32H745** dual-core (Cortex-M7 + Cortex-M4) microcontroller.
- **Isolated power rails** and impedance-controlled routing for Ethernet and USB.
- **MOV-based surge protection** on the measurement inputs.
- **On-board eMMC** storage for logging, plus Ethernet connectivity.

The current firmware and host workflows target a **6-channel acquisition mask (`0x3F`)** — three voltage and three current channels. See [`hardware/README.md`](hardware/README.md) for the board description, bill of materials, and manufacturing notes.

> **Note on voltage range:** the present hardware is validated for direct measurement at the
> board's rated input topology. Higher line-to-line service voltages (e.g. 480 V L-L) require updating the MOV-based surge protector or an external potential transformer, consistent with the modular design; see
> [`hardware/FutureUpdates/`](hardware/FutureUpdates/).

---

## Getting started

The fastest path depends on what you want to do.

**Bring up a board and capture data**

1. Read the system split in [`firmware/docs/Software/README.md`](firmware/docs/Software/README.md).
2. Build and flash the CM4/CM7 firmware from STM32CubeIDE ([`firmware/`](firmware/)).
3. Confirm connectivity with `firmware/UnitTests/tcptest.py` or the Python examples.
4. Use the [`board_interface`](firmware/BoardInterfaceLibrary/) library for repeatable host-side
   control, logging, and streaming.

**Reproduce or modify the hardware**

1. Open the KiCad project in [`hardware/`](hardware/).
2. Review the interactive BOM at `hardware/DesignSummary/ibom.html` and the stackup/impedance
   summaries.
3. Use the [`hardware/Manufacturing/`](hardware/Manufacturing/) outputs for fabrication.

**Deploy a permanent Raspberry Pi server**

See [`firmware/BoardInterfaceLibrary/docs/raspberry_pi_server.md`](firmware/BoardInterfaceLibrary/docs/raspberry_pi_server.md)
for the `aems-boardd` systemd daemon and `aemsctl` control command, and
[`cloud_autonomy.md`](firmware/BoardInterfaceLibrary/docs/cloud_autonomy.md) for optional AWS
IoT integration.

---

## Repository provenance

`hardware/` and `firmware/` are maintained as independent projects and integrated here via
`git subtree`. Each retains its own commit history and can be developed in isolation.

---

## Citation

If you use AEMS in academic work, please cite the paper and (optionally) the archived software
release. Machine-readable metadata is in [`CITATION.cff`](CITATION.cff).

```bibtex
@misc{selvaraj_aems_2026,
  title   = {An Open-Source, Autonomous Platform for High-Resolution Energy
             Monitoring in Manufacturing},
  author  = {Selvaraj, Vignesh and Nagaraj, Aditya and Zhang, Shengyuan and
             Sadeghian, Sina and Min, Sangkee},
  year    = {2026},
  note    = {Preprint. Department of Mechanical Engineering,
             University of Wisconsin--Madison}
  % arXiv id / journal / doi to be added
}
```

A citable, versioned archive of this repository (e.g. via Zenodo) will be linked here on
release: **DOI: _to be added_**.

---

## License

AEMS is intended for release as open hardware and open-source software. **License terms are
being finalized** — see [`LICENSE`](LICENSE) for the current status and the intended
hardware / software / documentation split. Until the final licenses are committed, please
contact the authors before redistribution.

---

## Contact

Lead developer: **Vignesh Selvaraj** — `vselvaraj@wisc.edu`, 
Department of Mechanical Engineering, University of Wisconsin–Madison.
