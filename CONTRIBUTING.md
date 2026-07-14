# Contributing to AEMS

Thanks for your interest in the Autonomous Energy Monitoring System (AEMS). Contributions,
questions, and reproductions are welcome — this project exists to make high-fidelity energy
monitoring accessible and reproducible for the manufacturing community.

## Repository structure

AEMS integrates two independently maintained subtrees via `git subtree`:

- [`hardware/`](hardware/) — KiCad design for the AEMS v2 board.
- [`firmware/`](firmware/) — STM32H745 firmware and the host-side Python `BoardInterfaceLibrary`.

Please keep changes scoped to one subtree per pull request where possible.

## Ways to contribute

- **Report issues** — hardware errata, firmware bugs, or documentation gaps. Include board
  revision, firmware core (CM4/CM7), host OS, and steps to reproduce.
- **Hardware** — schematic/layout improvements, alternative front-end options, or added parts.
  Update the interactive BOM and any affected `DesignSummary/` exports.
- **Firmware** — follow the CM4 (acquisition/storage) vs CM7 (network/control) split. Note the
  current 6-channel (`0x3F`) mask assumption when touching DAQ paths.
- **Host software** — the Python library targets stable, importable APIs; keep response parsing
  in `ResponseParser` and document new commands in `examples/README.md`.

## Development notes

- Firmware is built and flashed from **STM32CubeIDE**; network identity is code-owned in
  `firmware/CM7/Core/Inc/aems_network_config.h`.
- Hardware requires **KiCad 7+**; libraries are project-local via `sym-lib-table` / `fp-lib-table`.

## Pull requests

1. Fork and branch from the default branch.
2. Keep commits focused and describe *why*, not just *what*.
3. For firmware, confirm the board still builds and passes a basic bench connectivity check
   (`firmware/UnitTests/tcptest.py` or a `board_interface` example).
4. Update the relevant README/docs alongside code or design changes.

## License of contributions

The project's open hardware/software license is being finalized (see [`LICENSE`](LICENSE)). By
contributing, you agree that your contributions will be licensed under the project's eventual
license(s). If you have concerns, please raise them before submitting.

## Contact

Lead developer: **Vignesh Selvaraj** — `vselvaraj@wisc.edu` or `vignesh-selvaraj@outlook.com`
