# Examples

This directory contains runnable examples for `BoardInterfaceLibrary`. The examples are intentionally small and direct, but they cover the main workflows you are likely to need:

- inspecting a board and exercising individual commands (Comment out items you don't need)
- starting a DAQ log to eMMC and stopping it cleanly
- streaming a file or DAQ data back to the host
- running simultaneous streams from multiple boards
- running simultaneous DAQ logs on multiple boards

The host machine runs these scripts as the TCP server. Boards connect into the host as TCP clients.



## Response parsing for end users

Use `board_interface.ResponseParser` to convert board responses and ACKs into stable dictionaries for application code.

Example:

```python
from board_interface import ResponseParser
parsed = ResponseParser.parse(response)
```

This parser is the intended place to update when board ACK/response formats change in the future.

## Current board limitation

The current board/firmware build is limited to a channel mask of `0x3F`. All the binary to csv conversions implemented will only consider 6-channels, the codebase needs to be updated if going more than 6-channels.

That means:
- only channels 0 through 5 are currently supported by the examples and library workflows
- do not request a wider channel mask until the board firmware explicitly supports it

## TODO - Mandatory protocol fix

### Temporary workaround currently in use

The library now uses a temporary host-side heuristic, copied from the behavior proven in `UnitTests/tcptest.py`, to distinguish command `12` ACK traffic from command `13` DAQ stream payload on the same TCP socket.

What that means:
- while a DAQ stream is active, the host scans the incoming byte stream for something that looks like a fixed 100-byte control packet
- if it finds a plausible command `12` packet, it treats that region as control traffic instead of DAQ payload
- this is only a temporary compatibility fix so the example flow can stop a DAQ stream and continue collecting metrics

### Why this must be removed later

This is not a clean protocol boundary. It relies on packet-shape heuristics rather than explicit framing. That means it is workable for now, but it is not the correct long-term design.

### Required reliable fix - firmware/protocol update

A firmware update is required to make this reliable.

Mandatory fix:
- update the firmware/protocol so command `13` DAQ stream traffic is explicitly framed in chunks, or otherwise separated from fixed control/ACK packets
- after that, the host can parse command `12`, `10`, `110`, and other control packets deterministically during an active DAQ stream without any heuristic scanning

Recommended firmware-level solutions, in order of preference:
1. frame every DAQ stream chunk as a proper packet with command + length + payload
2. use a unified packet envelope for both stream and control messages
3. use separate logical or physical channels for stream data vs control traffic

Until that firmware update exists, the current heuristic should be treated as a temporary compatibility layer only.

## Directory contents

- `common.py`
  - shared helpers used by the single-board examples
- `commands_example.py`
  - runs the non-streaming commands against one board
- `daq_log_example.py`
  - starts DAQ logging on one board, waits, stops, then queries status and file size
- `stream_example.py`
  - streams either a remote file or a DAQ stream from one board
- `stream_on_command.py`
  - same core arguments as `stream_example.py`, but DAQ/file streaming starts when you press Enter
  - for DAQ mode, pressing Enter a second time sends command `12` to stop the stream
- `stream_gui.py`
  - starts a live DAQ stream and displays 0.25 second RMS values in a Tkinter GUI
  - shows channels 0 through 5 as Voltage-Ph1, Voltage-Ph2, Voltage-Ph3, Current-Ph1, Current-Ph2, Current-Ph3
- `multi_board_stream_example.py`
  - waits for multiple boards and starts simultaneous streams on all of them
- `multi_board_daq_log_example.py`
  - waits for multiple boards and starts simultaneous DAQ logs on all of them
- `boards.txt`
  - sample board IP file for the multi-board examples

## Requirements

- Python 3.10+
- the `board_interface` package available from this repo
- the host machine reachable by the boards over TCP
- boards configured to connect to the host on port `10` unless you override it

## General usage model

All examples follow the same pattern:

1. The script opens a TCP server on the host.
2. One or more boards connect to that server.
3. The script obtains a `BoardSession` for each board.
4. Commands are executed against each board session independently.

Single-board examples either:
- wait for the next board to connect, or
- wait for a specific board IP if `--board-ip` is given.

Multi-board examples:
- read a text file of IP addresses,
- wait for exactly those boards to connect,
- then start the requested operation on each board.

The default connection wait timeout used by the examples is 30 seconds unless you override `--timeout`.

## How to run the examples

Run from the `BoardInterfaceLibrary` directory:

```powershell
cd BoardInterfaceLibrary
```

### 1. Exercise the non-streaming commands

This example covers:
- heartbeat
- write config
- file counts
- file list
- file size
- DAQ status
- DAQ log status
- OpenAMP heartbeat
- optional delete file
- delete all `.bin` / `.dat`

```powershell
python .\examples\commands_example.py
```

Target a specific board:

```powershell
python .\examples\commands_example.py --board-ip 192.168.0.10
```

Use a specific file for command 5:

```powershell
python .\examples\commands_example.py --board-ip 192.168.0.10 --read-file daq.bin
```

Run an explicit delete-file before the delete-all:

```powershell
python .\examples\commands_example.py --board-ip 192.168.0.10 --delete-file daq_old.bin
```

### 2. Run a DAQ log on one board

This example starts logging to eMMC, waits for the chosen duration, stops logging, then reads DAQ status and file size.

```powershell
python .\examples\daq_log_example.py --board-ip 192.168.0.10 --filename daq0.bin --duration 15
```

Common options:
- `--sample-rate`
- `--channel-mask`
- `--block-samples`

Example:

```powershell
python .\examples\daq_log_example.py --board-ip 192.168.0.10 --filename daq0.bin --duration 20 --sample-rate 6000 --channel-mask 0x3F --block-samples 128
```

### 3. Stream from one board

#### Interactive Enter-to-start / Enter-to-stop streaming

This example is intended for manual bench use.

- it accepts the same main stream arguments as `stream_example.py`
- it waits for Enter before starting the stream
- in `--mode daq`, it waits for Enter again before sending command `12`
- in `--mode file`, the second Enter concept does not apply because file streaming ends when the remote file transfer completes

DAQ stream with manual start/stop:

```powershell
python .\examples\stream_on_command.py --mode daq --daq-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.csv
```

Raw file stream with manual start:

```powershell
python .\examples\stream_on_command.py --mode file --file-output-format bin --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.bin
```

#### Live RMS GUI

This example starts a command `13` live DAQ stream and opens a Tkinter GUI. Incoming stream frames are decoded directly in memory; no `.csv` or `.bin` file is written.

The GUI displays RMS values over 0.25 second windows:

- `ch0` -> `Voltage-Ph1`
- `ch1` -> `Voltage-Ph2`
- `ch2` -> `Voltage-Ph3`
- `ch3` -> `Current-Ph1`
- `ch4` -> `Current-Ph2`
- `ch5` -> `Current-Ph3`

Stop the stream with the GUI's `Stop Stream` button.

```powershell
python .\examples\stream_gui.py --mode daq --board-ip 192.168.0.10 --remote-file daq0.bin --sample-rate 2000 --channel-mask 0x3F --block-samples 128
```

#### Stream a remote file to the host

For `--mode file`, there are now two output options:

- `--file-output-format bin`
  - writes the remote file exactly as raw bytes to a `.bin` file
- `--file-output-format csv`
  - treats the remote file as a command `11` eMMC DAQ log file
  - decodes it using the CM4 packed-log format, not the live stream format
  - columns are:
    - `sample_index`
    - `ch0`
    - `ch1`
    - `ch2`
    - `ch3`
    - `ch4`
    - `ch5`

Raw binary file read:

```powershell
python .\examples\stream_example.py --mode file --file-output-format bin --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.bin
```

Convert streamed file directly to CSV:

```powershell
python .\examples\stream_example.py --mode file --file-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --output captures\daq0.csv
```

For file-mode CSV output, the example also writes a metadata file next to the CSV, for example:
- `captures\daq0.csv.metadata.json`

That metadata records:
- `decode_format`
  - currently `command11_packed_log`
- `sample_bytes`
  - currently `24`
- `channel_mask`
  - currently `0x3F`
- `assumed_channel_order`
- `channel_types`
- `command_8_stream`
  - parsed file-stream summary from the library
- `frames_written`

If `--output` is omitted, the script writes to a default path that includes the board IP.

##### Important binary-layout detail for file mode

When command `11` logs DAQ data to eMMC, the CM4 firmware writes only the selected channel values into the file:

- current board limitation: channel mask is `0x3F`
- channels written: `ch0` through `ch5`
- storage type: signed 32-bit little-endian per channel
- no `response` field in the file
- no `crc` field in the file
- no per-sample header

So the current board writes:
- `24` bytes per logged sample
- sample layout:
  - `ch0 int32_le`
  - `ch1 int32_le`
  - `ch2 int32_le`
  - `ch3 int32_le`
  - `ch4 int32_le`
  - `ch5 int32_le`

This is why file-mode CSV conversion uses a different unpacker from live DAQ stream CSV conversion.

#### Stream live DAQ data to the host

For `--mode daq`, there are now two output options:

- `--daq-output-format csv`
  - writes a parsed CSV with converted physical values
  - decodes the command `13` live stream frame format
  - columns are:
    - `sample_index`
    - `ch0`
    - `ch1`
    - `ch2`
    - `ch3`
    - `ch4`
    - `ch5`
  - channel mapping is:
    - `ch0`, `ch1`, `ch2` -> converted voltage values
    - `ch3`, `ch4`, `ch5` -> converted current values
- `--daq-output-format bin`
  - writes the raw DAQ stream payload directly to a `.bin` file
  - this file contains only raw bytes; you can re-run the same conversion methods later if you want to post-process it offline

##### Important binary-layout detail for DAQ stream mode

Command `13` live streaming is not packed the same way as the eMMC log file.

The live DAQ stream uses the full firmware sample frame:
- `response uint16_le`
- `crc uint16_le`
- `channel[0..7]` as eight signed 32-bit little-endian integers

Total:
- `36` bytes per stream frame

So:
- command `11` file log decode path != command `13` live stream decode path
- the library now keeps those two formats separate on purpose

The example starts command 13 asynchronously, waits for `--duration`, sends command 12 with `stop_daq()`, waits for stream completion, then sends command 10 and saves metadata next to the output file.

The default DAQ stream duration is 30 seconds.

```powershell
python .\examples\stream_example.py --mode daq --daq-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --duration 30 --output captures\daq0.csv
```

The example also writes a metadata file next to the CSV, for example:
- `captures\daq0.csv.metadata.json`

To dump the raw DAQ stream payload as a binary file instead:

```powershell
python .\examples\stream_example.py --mode daq --daq-output-format bin --board-ip 192.168.0.10 --remote-file daq0.bin --duration 30 --output captures\daq0.bin
```

That metadata file contains:
- `board_ip`
- `remote_file`
- `output_file`
- `duration_seconds`
- `command_13_ack`
  - fixed command 13 ACK if present, otherwise `null`
- `command_12_ack`
  - parsed stop ACK fields from command 12
  - includes `channel_mask_hex`
- `command_10_status`
  - parsed DAQ status returned by command 10 after the stream stops
  - includes `bytes_written_mib`
- `command_13_stream`
  - host-side DAQ stream summary
  - includes:
    - `bytes_received`
    - `bytes_received_mib`
    - `frames_received`
    - `elapsed_seconds`
    - `avg_frames_per_sec`
    - `avg_mib_per_sec`
    - `csv_path`

This metadata structure is intended to mirror the style of output you inspect in `tcptest.py` for commands 12, 13, and 10, but in machine-readable JSON form.

With DAQ stream settings:

```powershell
python .\examples\stream_example.py --mode daq --daq-output-format csv --board-ip 192.168.0.10 --remote-file daq0.bin --duration 15 --sample-rate 2000 --channel-mask 0x3F --block-samples 128
```

### 4. Stream from multiple boards simultaneously (Needs Testing)

Create an IP file such as `boards.txt`:

```text
192.168.0.10
192.168.0.11
192.168.0.12
```

A sample file is already included at:
- `examples/boards.txt`

#### Multi-board DAQ stream to CSV

```powershell
python .\examples\multi_board_stream_example.py --ip-file .\examples\boards.txt --mode daq --daq-output-format csv --remote-file daq.bin --output-dir captures
```

Each board writes to its own file, for example:
- `captures\192.168.0.10_daq.csv`
- `captures\192.168.0.11_daq.csv`

#### Multi-board DAQ stream to raw binary files

```powershell
python .\examples\multi_board_stream_example.py --ip-file .\examples\boards.txt --mode daq --daq-output-format bin --remote-file daq.bin --output-dir captures
```

Each board writes to its own file, for example:
- `captures\192.168.0.10_daq.bin`
- `captures\192.168.0.11_daq.bin`

#### Multi-board file stream to binary files

```powershell
python .\examples\multi_board_stream_example.py --ip-file .\examples\boards.txt --mode file --file-output-format bin --remote-file daq.bin --output-dir captures
```

Each board writes to its own file, for example:
- `captures\192.168.0.10_daq.bin`
- `captures\192.168.0.11_daq.bin`

#### Multi-board file stream converted to CSV

```powershell
python .\examples\multi_board_stream_example.py --ip-file .\examples\boards.txt --mode file --file-output-format csv --remote-file daq.bin --output-dir captures
```

Each board writes to its own converted CSV, for example:
- `captures\192.168.0.10_daq.csv`
- `captures\192.168.0.11_daq.csv`

For file-mode CSV conversion, each board also gets its own metadata file, for example:
- `captures\192.168.0.10_daq.csv.metadata.json`
- `captures\192.168.0.11_daq.csv.metadata.json`

These metadata files contain the same decode details as the single-board file-mode CSV example:
- `decode_format`
- `sample_bytes`
- `channel_mask`
- `assumed_channel_order`
- `channel_types`
- `command_8_stream`
- `frames_written`

### 5. Run DAQ logs on multiple boards simultaneously

This example starts logging on all listed boards first, waits for the requested duration, then stops each board and queries status and file size.

```powershell
python .\examples\multi_board_daq_log_example.py --ip-file .\examples\boards.txt --filename-prefix daq --duration 15
```

Generated remote filenames are unique per board, for example:
- `daq_192_168_0_10.bin`
- `daq_192_168_0_11.bin`

With a higher sample rate:

```powershell
python .\examples\multi_board_daq_log_example.py --ip-file .\examples\boards.txt --filename-prefix runA --duration 20 --sample-rate 6000 --channel-mask 0x3F --block-samples 128
```

## Command mapping reference

These examples exercise the current command set in the library:

- `0` - heartbeat
- `1` - write config
- `2` - `.dat` file count
- `3` - all file count
- `4` - file list with size
- `5` - file size for a specific filename
- `6` - delete all `.bin` / `.dat`
- `7` - delete a specific file
- `8` - file stream
- `9` - test stream
- `10` - DAQ status
- `11` - start DAQ log
- `12` - stop DAQ
- `13` - DAQ stream
- `97` - get current CM4 ADC offset calibration values
- `98` - run CM4 ADC offset calibration, save the offsets persistently, and return the new values
- `99` - OpenAMP heartbeat
- `110` - DAQ log status
- `112` - stop/close DAQ log

## Notes on outputs

### Host-side outputs

For stream examples, host-side output names are board-scoped so simultaneous runs do not collide.

Examples:
- `captures\192.168.0.10_daq.csv`
- `captures\192.168.0.11_daq.csv`
- `captures\192.168.0.10_daq.bin`

### Board-side filenames

For DAQ logging examples, the filename refers to the file created on the board eMMC.

Single-board example:
- `--filename daq0.bin`

Multi-board example:
- generated from `--filename-prefix` and board IP so each board gets a unique remote file name

## Practical workflow

A typical workflow is:

1. Use `commands_example.py` to confirm the board is connected and responsive.
2. Use `daq_log_example.py` to create a DAQ file on the board.
3. Use `stream_example.py` with `--mode file` or `--mode daq` to pull data back to the host.
4. Use the multi-board examples once the single-board path is confirmed.

## Troubleshooting

### A script waits forever for a board

Check:
- the board is configured to connect to the host IP and port
- the host firewall allows inbound TCP on the selected port
- the IP in `--board-ip` or `boards.txt` matches the board client IP

### A board connects but the wrong board is used

For single-board examples, provide `--board-ip` explicitly.

For multi-board examples, only the IPs listed in the IP file are accepted as targets.

### Output files are empty or incomplete

Check:
- the remote filename exists on the board
- command 11 actually created the DAQ file before command 8/13 is used
- DAQ log stop/close completed successfully before streaming the file

### Multiple boards are expected but only some connect

The multi-board examples wait for every IP in the IP file. Remove unavailable boards from the file or bring them online before starting the script.
