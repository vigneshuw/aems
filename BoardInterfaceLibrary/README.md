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

## Current capabilities

The library currently supports:
- single-board and multi-board TCP sessions keyed by board IP address
- DAQ logging control on the board eMMC
- remote file streaming and live DAQ streaming
- host-side parsing of ACKs/responses into stable dictionaries with `ResponseParser`
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
