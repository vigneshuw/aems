# aemsctl Command Reference

`aemsctl` is the Raspberry Pi shell interface for the permanent AEMS board
daemon. It talks to `aems-boardd` over a local Unix socket.

Default socket:

```bash
/run/aems-server/aems-boardd.sock
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
