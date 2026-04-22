from __future__ import annotations

import json
from dataclasses import asdict, is_dataclass
from pathlib import Path

from common import build_parser, ensure_directory, open_server, sleep_with_progress, wait_for_session


def to_serializable(value):
    if is_dataclass(value):
        return {key: to_serializable(item) for key, item in asdict(value).items()}
    if isinstance(value, bytes):
        return {
            "type": "bytes",
            "length": len(value),
            "hex_preview": value[:32].hex(),
        }
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, dict):
        return {key: to_serializable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [to_serializable(item) for item in value]
    return value


def metadata_path_for(output: Path) -> Path:
    return output.with_suffix(output.suffix + ".metadata.json")

def ack_summary(ack):
    if ack is None:
        return None
    data = to_serializable(ack)
    if isinstance(data, dict) and "channel_mask" in data:
        data["channel_mask_hex"] = f"0x{data['channel_mask']:08X}"
    return data


def daq_status_summary(status):
    data = to_serializable(status)
    if not isinstance(data, dict):
        return data
    bytes_written = data.get("bytes_written", 0)
    samples = data.get("samples_captured", 0)
    elapsed = float(data.get("epoch_time", 0))
    data["bytes_written_mib"] = round(bytes_written / (1024.0 * 1024.0), 6)
    data["samples_captured"] = samples
    return data


def daq_stream_summary(result):
    data = to_serializable(result)
    if not isinstance(data, dict):
        return data
    elapsed = float(data.get("elapsed_seconds", 0.0) or 0.0)
    frames = int(data.get("frames_received", 0) or 0)
    bytes_received = int(data.get("bytes_received", 0) or 0)
    data["avg_frames_per_sec"] = round((frames / elapsed), 3) if elapsed > 0.0 else 0.0
    data["avg_mib_per_sec"] = round(((bytes_received / (1024.0 * 1024.0)) / elapsed), 6) if elapsed > 0.0 else 0.0
    data["bytes_received_mib"] = round(bytes_received / (1024.0 * 1024.0), 6)
    return data


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
            print(f"Complete: {result.bytes_received} bytes written to {output}")
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
            print(start_ack)
        except TimeoutError:
            start_ack = None
            print("No fixed command 13 ACK received; continuing with stream header only.")

        print(f"Streaming for {args.duration:.1f}s before sending stop_daq()")
        sleep_with_progress(args.duration)

        print("Stopping DAQ stream")
        stop_ack = session.stop_daq(timeout=max(args.timeout, 10.0))
        print(stop_ack)

        result = handle.wait(timeout=max(args.timeout, 30.0))
        print(f"Complete: {result.frames_received} frames written to {output}")

        # Collect board-side DAQ metrics after the stop ACK and store them next to the streamed CSV.
        daq_status = session.get_daq_status(log_status=False, timeout=max(args.timeout, 10.0))
        metadata = {
            "board_ip": session.ip_address,
            "remote_file": args.remote_file,
            "output_file": str(output),
            "duration_seconds": args.duration,
            "command_13_ack": ack_summary(start_ack),
            "command_12_ack": ack_summary(stop_ack),
            "command_10_status": daq_status_summary(daq_status),
            "command_13_stream": daq_stream_summary(result),
        }
        metadata_path = metadata_path_for(output)
        metadata_path.write_text(json.dumps(metadata, indent=2))
        print(f"Metadata saved: {metadata_path}")
    finally:
        server.close()


if __name__ == "__main__":
    main()
