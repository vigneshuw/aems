# Raspberry Pi Permanent Server

The Raspberry Pi server mode runs one permanent process that listens for AEMS
boards on TCP port `10`. Boards connect as TCP clients when they are powered or
reconnected. Users then control those boards locally through the `aemsctl` shell
command.

This is separate from normal library usage. You can still install and import
`board_interface` from Windows, macOS, or Linux for direct scripting.

## Architecture

```mermaid
flowchart LR
    B1[AEMS board 192.168.0.10] -->|TCP client :10| D[aems-boardd]
    B2[AEMS board 192.168.0.11] -->|TCP client :10| D
    D --> DB[(SQLite board/job registry)]
    D --> CAP[/captures + metadata + manifests/]
    AG[aems-cloud-agent] -->|Unix socket JSON API| D
    AG <-->|MQTT/shadow| AWS[AWS IoT Core]
    CLI[aemsctl] -->|Unix socket JSON API| D
```

## Installed Paths

Default Raspberry Pi paths:

- Config: `/etc/aems-server/config.toml`
- Database: `/var/lib/aems-server/aems.db`
- Captures: `/var/lib/aems-server/captures`
- Metadata: `/var/lib/aems-server/metadata`
- Manifests: `/var/lib/aems-server/manifests`
- Local API socket: `/run/aems-server/aems-boardd.sock`

## Install

From the `BoardInterfaceLibrary` directory on the Raspberry Pi:

```bash
sudo bash ./deploy/install_raspberry_pi.sh
```

The installer:

- creates the `aems` system user
- creates the `aems` group and adds the sudo user to it when possible
- creates `/etc/aems-server/certs`
- creates `/var/lib/aems-server`
- installs the package into `/opt/aems/venv`
- installs `/etc/aems-server/config.toml`
- installs and enables `aems-boardd.service`
- installs, but does not enable, `aems-cloud-agent.service`

Log out and back in after install if `aemsctl` reports socket permission errors.

The installer deliberately keeps cloud support disabled until AWS IoT
certificates and environment variables exist. To add cloud support later:

```bash
cd BoardInterfaceLibrary
sudo /opt/aems/venv/bin/pip install ".[cloud]"
sudo systemctl daemon-reload
```

## Service Control

```bash
sudo systemctl status aems-boardd
sudo systemctl restart aems-boardd
sudo journalctl -u aems-boardd -f
```

TCP port `10` is privileged on Linux. The systemd unit grants only
`CAP_NET_BIND_SERVICE`, so the Python daemon can bind port `10` without running
the whole process as root.

Optional cloud agent service:

```bash
sudo systemctl enable aems-cloud-agent
sudo systemctl start aems-cloud-agent
sudo systemctl status aems-cloud-agent --no-pager
sudo journalctl -u aems-cloud-agent -f
```

Do not enable the cloud agent until `/etc/aems-server/cloud-agent.env` and the
AWS IoT certificate files are configured. See `cloud_autonomy.md`.

## Runtime Model

- `aems-boardd` owns TCP port `10`.
- Each board is keyed by IP address for now.
- The daemon stores board first-seen, last-seen, active state, connection count,
  and recent responses in SQLite.
- `aemsctl` never opens TCP port `10`; it talks to `/run/aems-server/aems-boardd.sock`.
- Long-running DAQ streams run as daemon jobs, not foreground shell processes.
- Schedules, transfer jobs, and health/shadow snapshots are stored in SQLite.
- `aems-cloud-agent` is optional and only talks to `aems-boardd` through the
  local Unix socket; it does not own board TCP sockets.

## Common Commands

```bash
aemsctl server status
aemsctl health --poll
aemsctl shadow --poll
aemsctl boards
aemsctl boards --active
aemsctl jobs
aemsctl jobs --active
aemsctl schedules
aemsctl transfers
```

Single-board checks:

```bash
aemsctl board 192.168.0.10 heartbeat
aemsctl board 192.168.0.10 openamp
aemsctl board 192.168.0.10 status
```

Multi-board DAQ stream:

```bash
aemsctl daq stream start --board all --file daq.bin --format bin --duration 30
aemsctl jobs --active
```

Manual stop:

```bash
aemsctl daq stream stop --board 192.168.0.10
aemsctl daq stream stop
```

eMMC DAQ log:

```bash
aemsctl daq log start --board 192.168.0.10 --file daq.bin --sample-rate 2000 --channel-mask 0x3F
aemsctl daq log stop --board 192.168.0.10
```

Timed eMMC DAQ log:

```bash
aemsctl daq log run --board all --file "daq_{board_ip}_{date}.bin" --duration 300
```

Scheduled autonomous DAQ:

```bash
aemsctl schedule add daily-stream --board all --mode daq_stream --start 02:00 --duration 300 --file-template "daq_{board_ip}_{date}.bin"
aemsctl schedules
```

Transfer completed captures:

```bash
aemsctl transfer start --target local --dest /mnt/usb/aems-upload
aemsctl transfer start --target s3 --bucket my-aems-data-bucket --prefix site-001/pi-001/
```

## Metadata

DAQ stream jobs write a metadata JSON file next to the output file. It includes:

- board IP
- requested DAQ config
- command `13` stream result
- command `12` stop ACK
- command `10` DAQ status
- throughput metrics
- dropped-buffer estimates

Transfer jobs create a manifest JSON under `/var/lib/aems-server/manifests`.
The manifest includes byte counts and SHA-256 hashes for every transferred file.
It is transferred last so cloud ingestion can treat the manifest as the
"complete bundle is available" signal.

For AWS IoT Core, S3, device shadow, and cloud-triggered workflows, see
`cloud_autonomy.md`.

## Install Modes Summary

| Use case | Command |
|---|---|
| Python scripting only | `pip install .` |
| Raspberry Pi permanent daemon | `sudo bash ./deploy/install_raspberry_pi.sh` |
| Add S3 / AWS IoT dependencies to Pi venv | `sudo /opt/aems/venv/bin/pip install ".[cloud]"` |
| Reinstall all extras into current environment | `pip install ".[daemon,cloud]"` |

Run the `pip install .[...]` commands from the `BoardInterfaceLibrary` directory
so `pip` can read this package's `pyproject.toml`.

After updating the package on an already-installed Pi, restart the daemon:

```bash
sudo systemctl restart aems-boardd
```

If the update changed board protocol framing, flash matching firmware before
using the daemon with real boards.

## Current Board Limitation

The current board firmware is limited to channel mask `0x3F`, meaning channels
`0..5` are active. The daemon and CLI allow the channel mask to be passed through,
but current production usage should keep `--channel-mask 0x3F`.

## Troubleshooting

If `aemsctl` cannot connect:

```bash
sudo systemctl status aems-boardd
ls -l /run/aems-server/aems-boardd.sock
sudo journalctl -u aems-boardd -n 100
```

If no boards are active:

- confirm the Raspberry Pi Ethernet interface is on the same subnet
- confirm no other process owns TCP port `10`
- power-cycle or reset the board
- check `aemsctl boards` for previous connection timestamps

If DAQ stream jobs remain running:

```bash
aemsctl jobs --active
aemsctl daq stream stop
```
