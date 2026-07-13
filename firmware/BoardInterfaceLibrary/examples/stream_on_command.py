from __future__ import annotations

import json
from pathlib import Path

from board_interface import ResponseParser, convert_daq_bin_to_csv
from common import build_parser, ensure_directory, open_server, print_parsed_response, wait_for_session

FILE_LOG_DECODE_FORMAT = "command11_packed_log"
FILE_LOG_SAMPLE_BYTES = 24
FILE_LOG_CHANNEL_MASK = 0x3F


def metadata_path_for(output: Path) -> Path:
    return output.with_suffix(output.suffix + ".metadata.json")


def count_enabled_channels(channel_mask: int) -> int:
    return bin(channel_mask & 0xFFFFFFFF).count("1")


def wait_for_enter(prompt: str) -> None:
    input(prompt)


def main() -> None:
    parser = build_parser("Start streaming on Enter, stop DAQ streaming on Enter")
    parser.add_argument("--mode", choices=["file", "daq"], default="daq", help="Streaming mode")
    parser.add_argument("--remote-file", default="daq.bin", help="Remote filename to stream")
    parser.add_argument("--output", help="Local output path. Defaults to captures/<ip>_<name>.(bin|csv)")
    parser.add_argument("--file-output-format", choices=["bin", "csv"], default="bin", help="For --mode file: write raw binary file or convert DAQ file to parsed CSV")
    parser.add_argument("--daq-output-format", choices=["csv", "bin"], default="csv", help="For --mode daq: write parsed channel CSV or raw binary stream")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ stream sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    server = open_server(args.host, args.port)
    try:
        session = wait_for_session(server, args.board_ip, args.timeout)

        if args.mode == "file":
            output_suffix = ".csv" if args.file_output_format == "csv" else Path(args.remote_file).suffix or ".bin"
            output = Path(args.output) if args.output else Path("captures") / f"{session.ip_address}_{Path(args.remote_file).stem}{output_suffix}"
            output = ensure_directory(output)
            stream_target = output if args.file_output_format == "bin" else ensure_directory(output.with_suffix(output.suffix + ".raw.bin"))

            wait_for_enter(f"Ready to stream file {args.remote_file} from {session.ip_address}. Press Enter to start...")
            print(f"Streaming file {args.remote_file} -> {output}")
            result = session.stream_file(
                filename=args.remote_file,
                local_path=stream_target,
                timeout=max(args.timeout, 60.0),
            )

            if args.file_output_format == "csv":
                frames_written = convert_daq_bin_to_csv(stream_target, output)
                metadata = {
                    "board_ip": session.ip_address,
                    "remote_file": args.remote_file,
                    "output_file": str(output),
                    "output_format": "csv",
                    "source_stream_command": 8,
                    "decode_format": FILE_LOG_DECODE_FORMAT,
                    "sample_bytes": FILE_LOG_SAMPLE_BYTES,
                    "channel_mask": FILE_LOG_CHANNEL_MASK,
                    "channel_mask_hex": f"0x{FILE_LOG_CHANNEL_MASK:08X}",
                    "enabled_channel_count": count_enabled_channels(FILE_LOG_CHANNEL_MASK),
                    "assumed_channel_order": ["ch0", "ch1", "ch2", "ch3", "ch4", "ch5"],
                    "channel_types": {
                        "ch0": "voltage",
                        "ch1": "voltage",
                        "ch2": "voltage",
                        "ch3": "current",
                        "ch4": "current",
                        "ch5": "current",
                    },
                    "command_8_stream": ResponseParser.parse(result),
                    "frames_written": frames_written,
                }
                metadata_path = metadata_path_for(output)
                metadata_path.write_text(json.dumps(metadata, indent=2))
                stream_target.unlink(missing_ok=True)
                print({"converted_csv": str(output), "frames_written": frames_written})
                print(f"Metadata saved: {metadata_path}")

            print_parsed_response(result)
            return

        default_suffix = ".csv" if args.daq_output_format == "csv" else ".bin"
        output = Path(args.output) if args.output else Path("captures") / f"{session.ip_address}_{Path(args.remote_file).stem}{default_suffix}"
        output = ensure_directory(output)

        wait_for_enter(f"Ready to start DAQ stream for {args.remote_file} from {session.ip_address}. Press Enter to start...")
        print(f"Starting DAQ stream for {args.remote_file} -> {output}")
        handle = session.start_daq_stream_async(
            filename=args.remote_file,
            sample_rate_hz=args.sample_rate,
            channel_mask=args.channel_mask,
            block_samples=args.block_samples,
            csv_path=output if args.daq_output_format == "csv" else None,
            local_path=output if args.daq_output_format == "bin" else None,
        )

        try:
            start_ack = session.wait_for_command(13, timeout=2.0)
            print_parsed_response(start_ack)
        except TimeoutError:
            start_ack = None
            print("No fixed command 13 ACK received; continuing with stream header only.")

        wait_for_enter("DAQ stream is active. Press Enter again to send stop_daq()...")
        print("Stopping DAQ stream")
        stop_ack = session.stop_daq(timeout=max(args.timeout, 10.0))
        print_parsed_response(stop_ack)

        result = handle.wait(timeout=max(args.timeout, 30.0))
        print_parsed_response(result)

        daq_status = session.get_daq_status(log_status=False, timeout=max(args.timeout, 10.0))
        metadata = {
            "board_ip": session.ip_address,
            "remote_file": args.remote_file,
            "output_file": str(output),
            "output_format": args.daq_output_format,
            "control_mode": "manual_enter_start_stop",
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
