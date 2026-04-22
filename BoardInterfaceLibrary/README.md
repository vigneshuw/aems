# Board Interface Library

This package provides a reusable Python interface for the AEMS board TCP protocol.

Primary APIs:
- `board_interface.BoardServer`
- `board_interface.BoardSession`
- `board_interface.StreamHandle`
- `board_interface.cli`

Examples are under `examples/`:
- `examples/commands_example.py`
- `examples/daq_log_example.py`
- `examples/stream_example.py`
- `examples/multi_board_stream_example.py`
- `examples/multi_board_daq_log_example.py`
- `examples/README.md`


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
