from __future__ import annotations

import json
from pathlib import Path

from board_interface import ResponseParser
from common import build_parser, ensure_directory, open_server, print_parsed_response, sleep_with_progress, wait_for_session


def metadata_path_for(output: Path) -> Path:
    return output.with_suffix(output.suffix + ".metadata.json")


def count_enabled_channels(channel_mask: int) -> int:
    return bin(channel_mask & 0xFFFFFFFF).count("1")


def main() -> None:
    parser = build_parser("Stream a remote file or DAQ stream from one board")
    parser.add_argument("--mode", choices=["file", "daq"], default="daq", help="Streaming mode")
    parser.add_argument("--remote-file", default="daq.bin", help="Remote filename to stream")
    parser.add_argument("--output", help="Local output path. Defaults to captures/<ip>_<name>.(bin|csv)")
    parser.add_argument("--duration", type=float, default=30.0, help="DAQ stream duration in seconds before command 12 is sent")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ stream sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    server = open_server(args.host, args.port)
    try:
        session = wait_for_session(server, args.board_ip, args.timeout)

        if args.mode == "file":
            # Command 8 style file stream: copy a remote file byte-for-byte to the host.
            output = Path(args.output) if args.output else Path("captures") / f"{session.ip_address}_{Path(args.remote_file).name}"
            output = ensure_directory(output)
            print(f"Streaming file {args.remote_file} -> {output}")
            result = session.stream_file(
                filename=args.remote_file,
                local_path=output,
                timeout=max(args.timeout, 60.0),
            )
            print_parsed_response(result)
            return

        # Command 13 style DAQ stream: start asynchronously, record the command 13 ack,
        # run for the requested duration, then send command 12 so the board closes the live stream cleanly.
        output = Path(args.output) if args.output else Path("captures") / f"{session.ip_address}_{Path(args.remote_file).stem}.csv"
        output = ensure_directory(output)
        print(f"Starting DAQ stream for {args.remote_file} -> {output}")
        handle = session.start_daq_stream_async(
            filename=args.remote_file,
            sample_rate_hz=args.sample_rate,
            channel_mask=args.channel_mask,
            block_samples=args.block_samples,
            csv_path=output,
        )
        # Some firmware builds return a fixed command 13 ACK before streaming, others transition
        # directly into the stream header. Treat the start ACK as optional for now.
        try:
            start_ack = session.wait_for_command(13, timeout=2.0)
            print_parsed_response(start_ack)
        except TimeoutError:
            start_ack = None
            print("No fixed command 13 ACK received; continuing with stream header only.")

        print(f"Streaming for {args.duration:.1f}s before sending stop_daq()")
        sleep_with_progress(args.duration)

        print("Stopping DAQ stream")
        stop_ack = session.stop_daq(timeout=max(args.timeout, 10.0))
        print_parsed_response(stop_ack)

        result = handle.wait(timeout=max(args.timeout, 30.0))
        print_parsed_response(result)

        # Collect board-side DAQ metrics after the stop ACK and store them next to the streamed CSV.
        daq_status = session.get_daq_status(log_status=False, timeout=max(args.timeout, 10.0))
        metadata = {
            "board_ip": session.ip_address,
            "remote_file": args.remote_file,
            "output_file": str(output),
            "duration_seconds": args.duration,
            "command_13_ack_present": start_ack is not None,
            "requested_daq_config": {
                "sample_rate_hz": args.sample_rate,
                "channel_mask": args.channel_mask,
                "channel_mask_hex": f"0x{args.channel_mask:08X}",
                "block_samples": args.block_samples,
                "enabled_channel_count": count_enabled_channels(args.channel_mask),
            },
            "confirmed_daq_config": ResponseParser.parse(start_ack),
            "command_13_ack": ResponseParser.parse(start_ack),
            "command_12_ack": ResponseParser.parse(stop_ack),
            "command_10_status": ResponseParser.parse(daq_status),
            "command_13_stream": ResponseParser.parse(result),
        }
        metadata_path = metadata_path_for(output)
        metadata_path.write_text(json.dumps(metadata, indent=2))
        print(f"Metadata saved: {metadata_path}")
    finally:
        server.close()


if __name__ == "__main__":
    main()
