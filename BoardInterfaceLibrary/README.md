# Board Interface Library

This package provides a reusable Python interface for the AEMS board TCP protocol.

Primary APIs:
- `board_interface.BoardServer`
- `board_interface.BoardSession`
- `board_interface.StreamHandle`
- `board_interface.ResponseParser`
- `board_interface.convert_daq_bin_to_csv`

Examples are under `examples/`:
- `examples/commands_example.py`
- `examples/daq_log_example.py`
- `examples/stream_example.py`
- `examples/stream_on_command.py`
- `examples/stream_gui.py`
- `examples/multi_board_stream_example.py`
- `examples/multi_board_daq_log_example.py`
- `examples/README.md`

Raspberry Pi daemon/service docs:
- `docs/raspberry_pi_server.md`
- `docs/aemsctl.md`
- `docs/cloud_autonomy.md`

## Current capabilities

The library currently supports:
- single-board and multi-board TCP sessions keyed by board IP address
- DAQ logging control on the board eMMC
- remote file streaming and live DAQ streaming
- host-side parsing of ACKs/responses into stable dictionaries with `ResponseParser`
- command `99` parsing for network diagnostics, including board IP, derived MAC address, configured server IP/port, local TCP source port, connect-attempt count, and last connect/socket errors
- raw binary DAQ capture and offline conversion to parsed CSV
- live Tkinter-based RMS visualization for DAQ stream data through the example layer

For streamed or logged DAQ data, there are now two host-side output styles:
- raw `.bin`
  - preserves the exact raw bytes from the board
  - suitable for later post-processing
- parsed `.csv`
  - converts each DAQ frame into physical values
  - columns are `sample_index,ch0,ch1,ch2,ch3,ch4,ch5`
  - channels `0,1,2` are converted as voltage
  - channels `3,4,5` are converted as current

The same conversion path is exposed through:

```python
from board_interface import convert_daq_bin_to_csv
```

## DAQ binary layout: eMMC log vs live stream

The board does not emit the same binary layout for command `11` eMMC logging and command `13` live DAQ streaming. The library now handles these as two separate formats.

### Command 11: eMMC DAQ log file layout

The CM4 firmware packs only the selected channel values into the file written to eMMC.

For the current board build:
- channel mask is limited to `0x3F`
- that means channels `0..5` are logged
- each channel is written as one signed 32-bit little-endian integer
- there is no frame header, no response field, and no CRC in the file

So each logged sample in the `.bin` file is:
- `ch0` `int32_le`
- `ch1` `int32_le`
- `ch2` `int32_le`
- `ch3` `int32_le`
- `ch4` `int32_le`
- `ch5` `int32_le`

Total:
- `6 * 4 = 24` bytes per sample

This is the format used by:

```python
from board_interface import convert_daq_bin_to_csv
```

When `examples/stream_example.py` is used in file mode with:

```powershell
python .\examples\stream_example.py --mode file --file-output-format csv ...
```

the example now writes a metadata file next to the CSV that records:
- `decode_format: "command11_packed_log"`
- `sample_bytes: 24`
- `channel_mask: 0x3F`
- assumed channel order and channel types
- parsed command `8` stream summary

The multi-board file-stream example does the same thing per board, writing one metadata JSON file next to each converted CSV.

### Command 13: live DAQ stream layout

The live DAQ stream uses the full firmware frame layout:
- `response` as `uint16_le`
- `crc` as `uint16_le`
- `channel[0..7]` as eight signed 32-bit little-endian integers

Total:
- `36` bytes per stream frame

This is a different format from the eMMC log file. The library keeps these decode paths separate so command `11` files are not misinterpreted as command `13` stream frames.

## Installation Modes

### Normal laptop / desktop library install

Use this mode on Windows, macOS, Linux, or a Raspberry Pi when you want to write
your own Python scripts and directly import `board_interface`.

```powershell
pip install .
```

This installs:

- `board_interface` importable Python package
- `board-interface-cli` legacy interactive CLI
- `aemsctl`, `aems-boardd`, and `aems-cloud-agent` console entry points

Only `board_interface` and examples are normally used in this mode. The daemon
entry points are useful mainly on the Raspberry Pi permanent server.

### Raspberry Pi daemon install

Use this mode when the Pi should permanently listen on TCP port `10` and accept
any AEMS board that connects over Ethernet.

From the `BoardInterfaceLibrary` directory on the Pi:

```bash
sudo bash ./deploy/install_raspberry_pi.sh
```

The installer:

- creates the `aems` system user and group
- adds the invoking sudo user to group `aems` when possible
- creates `/etc/aems-server`
- creates `/etc/aems-server/certs`
- creates `/var/lib/aems-server/{captures,metadata,manifests,transfer_out}`
- installs this package into `/opt/aems/venv`
- installs `/etc/aems-server/config.toml`
- installs `aems-boardd.service`
- installs, but does not enable, `aems-cloud-agent.service`
- enables and restarts `aems-boardd`

Check daemon and CLI:

```bash
sudo systemctl status aems-boardd --no-pager
aemsctl server status
aemsctl boards --active
```

If `aemsctl` reports socket permission errors, log out and back in so the `aems`
group membership refreshes.

### Optional cloud dependencies

The base daemon does not require AWS packages. Install cloud extras only when
you need S3 transfer or AWS IoT Core integration:

Run this from the `BoardInterfaceLibrary` directory:

```bash
sudo /opt/aems/venv/bin/pip install ".[cloud]"
```

This installs:

- `boto3` for S3 upload jobs
- `awsiotsdk` for `aems-cloud-agent`

You can also install all extras into a local environment:

```bash
pip install ".[daemon,cloud]"
```

## Quick example

Stream a board file directly to raw binary:

```powershell
python .\examples\stream_example.py --mode file --file-output-format bin --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.bin
```

Stream a board file and convert it directly to parsed CSV:

```powershell
python .\examples\stream_example.py --mode file --file-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.csv
```

Stream live DAQ data to parsed CSV:

```powershell
python .\examples\stream_example.py --mode daq --daq-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --duration 30 --output captures\daq0.csv
```

Open a live RMS GUI for the DAQ stream:

```powershell
python .\examples\stream_gui.py --mode daq --board-ip 192.168.0.10 --remote-file daq0.bin --sample-rate 2000 --channel-mask 0x3F --block-samples 128
```

Use `examples/README.md` for the full command reference and all example flows.

## Raspberry Pi permanent server

For a permanent Raspberry Pi server, install the daemon and use `aemsctl`.
The daemon owns TCP port `10`; boards connect to it automatically when they are
plugged into Ethernet.

Install on the Pi:

```bash
cd BoardInterfaceLibrary
sudo bash ./deploy/install_raspberry_pi.sh
```

Useful commands:

```bash
aemsctl server status
aemsctl boards --active
aemsctl board 192.168.0.10 openamp
aemsctl daq stream start --board all --file daq.bin --format bin --duration 30
aemsctl daq stream stop
```

The daemon stores board history and DAQ job metadata in SQLite under
`/var/lib/aems-server`. See `docs/raspberry_pi_server.md` and
`docs/aemsctl.md` for the full service and CLI reference.

Cloud/autonomous operation is documented in `docs/cloud_autonomy.md`. The daemon
now supports local schedules, health/shadow JSON, transfer manifests, local/S3
transfer jobs, and an optional AWS IoT bridge through `aems-cloud-agent`.

## Cloud Agent Summary

`aems-cloud-agent` is optional. It does not own board TCP connections; it talks
to the local daemon through `/run/aems-server/aems-boardd.sock`.

It does four things:

- subscribes to `aems/<thing-name>/commands/#`
- forwards supported cloud commands to `aems-boardd`
- publishes command responses to `aems/<thing-name>/responses`
- publishes the daemon shadow document to `$aws/things/<thing-name>/shadow/update`

Minimum cloud setup on the Pi:

```bash
sudo /opt/aems/venv/bin/pip install ".[cloud]"
sudo install -d -o aems -g aems -m 0750 /etc/aems-server/certs
sudo cp <device-cert.pem.crt> /etc/aems-server/certs/device.pem.crt
sudo cp <private-key.pem.key> /etc/aems-server/certs/private.pem.key
sudo cp <AmazonRootCA1.pem> /etc/aems-server/certs/AmazonRootCA1.pem
sudo chown -R aems:aems /etc/aems-server/certs
sudo chmod 0640 /etc/aems-server/certs/*
```

Create `/etc/aems-server/cloud-agent.env`:

```bash
AEMS_AWS_IOT_ENDPOINT=xxxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com
AEMS_AWS_THING_NAME=aems-site-001-pi-001
AEMS_AWS_CERT=/etc/aems-server/certs/device.pem.crt
AEMS_AWS_KEY=/etc/aems-server/certs/private.pem.key
AEMS_AWS_CA=/etc/aems-server/certs/AmazonRootCA1.pem
AEMS_AWS_TOPIC_PREFIX=aems/aems-site-001-pi-001
```

Enable and inspect:

```bash
sudo systemctl daemon-reload
sudo systemctl enable aems-cloud-agent
sudo systemctl start aems-cloud-agent
sudo systemctl status aems-cloud-agent --no-pager
journalctl -u aems-cloud-agent -f
```

Cloud command payloads are JSON. Publish them to
`aems/<thing-name>/commands/<request-id>`:

```json
{
  "command": "daq.stream.start",
  "params": {
    "board": "all",
    "file": "daq_cloud.bin",
    "format": "bin",
    "duration": 300,
    "sample_rate": 2000,
    "channel_mask": 63,
    "block_samples": 128
  }
}
```

Useful cloud commands mirror the local daemon API:

- `health.get` / `health.shadow`
- `boards.list`, `jobs.list`, `transfers.list`
- `daq.stream.start` / `daq.stream.stop`
- `daq.log.start` / `daq.log.stop` / `daq.log.run`
- `transfer.start`
- `schedule.add` / `schedule.remove` / `schedule.enable`

Responses are published to `aems/<thing-name>/responses`. The device shadow is
published to `$aws/things/<thing-name>/shadow/update` every 30 seconds by
default. Use `docs/cloud_autonomy.md` for the full AWS setup and command
reference.

The AWS IoT policy must allow MQTT connect, subscribe/receive on the command
topic, publish on the response topic, and publish to the named shadow update
topic. S3 uploads require AWS credentials available to the `aems` user and
`s3:PutObject` permission on the configured bucket/prefix.



## Response parsing for end users

Use `board_interface.ResponseParser` to convert board responses and ACKs into stable dictionaries for application code.

Example:

```python
from board_interface import ResponseParser
parsed = ResponseParser.parse(response)
```

This parser is the intended place to update when board ACK/response formats change in the future.

## Current board limitation

The current board/firmware build is limited to a channel mask of `0x3F`.

That means:
- only channels 0 through 5 are currently supported by the examples and library workflows
- do not request a wider channel mask until the board firmware explicitly supports it

## TODO - Mandatory protocol fix

The current library contains a temporary host-side workaround to distinguish command `12` ACK traffic from command `13` DAQ stream payload on the same TCP connection.

This is not a reliable protocol boundary. It exists only as a temporary compatibility fix.

Mandatory long-term fix:
- update the firmware/protocol so DAQ stream traffic is explicitly framed or otherwise separated from control packets
- once that firmware change is made, the host library should remove the heuristic stream/control marker scan and parse packets deterministically

See `examples/README.md` for the detailed explanation and usage impact.
