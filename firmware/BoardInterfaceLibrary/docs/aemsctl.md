# aemsctl Command Reference

`aemsctl` is the Raspberry Pi shell interface for the permanent AEMS board
daemon. It talks to `aems-boardd` over a local Unix socket.

Default socket:

```bash
/run/aems-server/aems-boardd.sock
```

## Installation

`aemsctl` is installed by the Raspberry Pi installer:

```bash
sudo bash ./deploy/install_raspberry_pi.sh
```

The executable is installed in:

```text
/opt/aems/venv/bin/aemsctl
```

The installer normally makes it available on the shell path. If not, call it
directly:

```bash
/opt/aems/venv/bin/aemsctl server status
```

`aemsctl` requires the daemon to be running:

```bash
sudo systemctl status aems-boardd --no-pager
```

Override when needed:

```bash
aemsctl --socket /tmp/aems-boardd.sock server status
```

## Server

```bash
aemsctl server status
```

Returns:

- listen host and port
- API socket path
- database path
- capture directories
- uptime
- active and known board counts

## Boards

```bash
aemsctl boards
aemsctl boards --active
```

Each board entry contains:

- IP address
- active state
- first seen timestamp
- last seen timestamp
- last connected/disconnected timestamps
- connection count
- last parsed heartbeat/openamp/status response, if available

## Jobs

```bash
aemsctl jobs
aemsctl jobs --active
```

Jobs currently cover DAQ streams started through the daemon.

## Health and Shadow

```bash
aemsctl health
aemsctl health --poll
aemsctl shadow
aemsctl shadow --poll
```

`health` returns a daemon/board/job/storage summary. `shadow` returns an AWS IoT
Device Shadow compatible document with `state.reported`.

Use `--poll` when you want the daemon to query connected boards before returning
the result. The daemon avoids health polling while it owns an active DAQ stream
job because the current firmware stream framing cannot safely interleave
arbitrary status responses into command `13` stream payloads.

## Schedules

```bash
aemsctl schedules
aemsctl schedules --enabled
```

Add a schedule:

```bash
aemsctl schedule add daily-stream \
  --board all \
  --mode daq_stream \
  --start 02:00 \
  --duration 300 \
  --file-template "daq_{board_ip}_{date}.bin" \
  --format bin
```

Enable/disable/remove:

```bash
aemsctl schedule disable daily-stream
aemsctl schedule enable daily-stream
aemsctl schedule remove daily-stream
```

Supported schedule modes:

- `daq_stream`: host-side command `13` stream job
- `daq_log`: board-side command `11` eMMC log with timed stop/close

## Transfers

```bash
aemsctl transfers
aemsctl transfers --active
```

Copy captures/metadata/manifests to a local destination:

```bash
aemsctl transfer start --target local --dest /mnt/usb/aems-upload
```

Upload captures/metadata/manifests to S3:

```bash
aemsctl transfer start --target s3 --bucket my-aems-data-bucket --prefix site-001/pi-001/
```

S3 transfer requires the optional cloud dependency and valid AWS credentials.
Run this from the `BoardInterfaceLibrary` directory:

```bash
sudo /opt/aems/venv/bin/pip install ".[cloud]"
```

## Board Commands

```bash
aemsctl board 192.168.0.10 heartbeat
aemsctl board 192.168.0.10 openamp
aemsctl board 192.168.0.10 status
```

These map to firmware commands:

- `heartbeat`: command `0`
- `openamp`: command `99`
- `status`: command `10`

## eMMC Commands

```bash
aemsctl emmc list --board 192.168.0.10
aemsctl emmc size --board 192.168.0.10 --file daq.bin
aemsctl emmc delete --board 192.168.0.10 --file daq.bin
aemsctl emmc delete-logs --board 192.168.0.10
```

Use `--board all` to run the operation on every currently connected board.

## DAQ Stream

Start one board:

```bash
aemsctl daq stream start --board 192.168.0.10 --file daq.bin --format bin --duration 30
```

Start every connected board:

```bash
aemsctl daq stream start --board all --file daq.bin --format csv --duration 30
```

### Where Stream Files Are Saved

`daq stream start` is a host-side live stream capture. The board streams command
`13` DAQ frames over TCP, and `aems-boardd` writes the received data on the
Raspberry Pi. It does not save this command `13` stream to the board eMMC.

Default output directory:

```text
/var/lib/aems-server/captures
```

Default metadata directory:

```text
/var/lib/aems-server/metadata
```

These paths come from `/etc/aems-server/config.toml`:

```toml
capture_dir = "/var/lib/aems-server/captures"
metadata_dir = "/var/lib/aems-server/metadata"
```

You can confirm the active paths with:

```bash
aemsctl server status
```

For this command:

```bash
aemsctl daq stream start --board all --file daq.bin --format csv --duration 30
```

the daemon creates one independent DAQ stream job per currently active board.
Each board gets its own output file. The filename pattern is:

```text
<board_ip_with_underscores>_<remote_file_stem>_<job_id_prefix>.<format>
```

Example for two boards:

```text
/var/lib/aems-server/captures/192_168_0_10_daq_a1b2c3d4.csv
/var/lib/aems-server/captures/192_168_0_11_daq_e5f6a7b8.csv
```

The matching metadata files are saved separately:

```text
/var/lib/aems-server/metadata/192_168_0_10_daq_a1b2c3d4.csv.metadata.json
/var/lib/aems-server/metadata/192_168_0_11_daq_e5f6a7b8.csv.metadata.json
```

For `--format csv`, the daemon first receives the raw command `13` stream into a
temporary raw binary file, converts it to physical-value CSV when the stream
stops, then deletes the temporary raw file. The final CSV contains parsed
physical values from the live stream. The metadata JSON records:

- job ID and board IP
- requested sample rate, channel mask, and block size
- output file path and output format
- command `13` stream summary
- command `12` stop ACK
- command `10` DAQ status after the stream stops
- received bytes, frames, throughput, dropped buffers, and loss estimates

For `--format bin`, the daemon keeps the raw command `13` stream bytes directly
as the final `.bin` file and writes the same metadata JSON next to it in the
metadata directory.

Use `--output-dir` to override only the capture output directory for this run:

```bash
aemsctl daq stream start --board all --file daq.bin --format csv --duration 30 --output-dir /mnt/usb/aems-captures
```

Metadata still goes to the configured daemon metadata directory.

Stop jobs:

```bash
aemsctl daq stream stop --board 192.168.0.10
aemsctl daq stream stop --job-id <job_id>
aemsctl daq stream stop
```

Arguments:

- `--format bin`: preserve raw command `13` stream bytes
- `--format csv`: convert command `13` stream frames to physical values
- `--duration`: optional automatic stop time
- `--sample-rate`: requested sample rate
- `--channel-mask`: current board limit is `0x3F`
- `--block-samples`: firmware block size, normally `128`

## eMMC DAQ Log

Start:

```bash
aemsctl daq log start --board 192.168.0.10 --file daq.bin --sample-rate 2000 --channel-mask 0x3F
```

Stop:

```bash
aemsctl daq log stop --board 192.168.0.10
```

Run a timed eMMC log as a daemon job:

```bash
aemsctl daq log run --board all --file "daq_{board_ip}_{date}.bin" --duration 3600
```

Status:

```bash
aemsctl daq status --board 192.168.0.10
aemsctl daq status --board 192.168.0.10 --log-status
```

## Calibration

```bash
aemsctl calibration get --board 192.168.0.10
aemsctl calibration run --board 192.168.0.10
```

Calibration run takes about 15 seconds on the current firmware.

## Output Format

Every command prints JSON:

```json
{
  "ok": true,
  "data": {}
}
```

Failures are also JSON:

```json
{
  "ok": false,
  "error": "Board 192.168.0.10 is not connected"
}
```
