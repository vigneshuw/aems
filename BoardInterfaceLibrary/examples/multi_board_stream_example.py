from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from board_interface import BoardServer, ResponseParser, StreamHandle, convert_daq_bin_to_csv, convert_daq_stream_bin_to_csv

FILE_LOG_DECODE_FORMAT = "command11_packed_log"
FILE_LOG_SAMPLE_BYTES = 24
FILE_LOG_CHANNEL_MASK = 0x3F
DAQ_STREAM_FRAME_BYTES = 36


def load_board_ips(path: Path) -> list[str]:
    # Each non-empty, non-comment line is treated as one target board IP.
    ips: list[str] = []
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        ips.append(line)
    return ips


def default_output_path(output_dir: Path, board_ip: str, remote_file: str, mode: str, daq_output_format: str, file_output_format: str) -> Path:
    if mode == "daq":
        suffix = ".csv" if daq_output_format == "csv" else ".bin"
        filename = f"{board_ip}_{Path(remote_file).stem}{suffix}"
    else:
        suffix = ".csv" if file_output_format == "csv" else (Path(remote_file).suffix or ".bin")
        filename = f"{board_ip}_{Path(remote_file).stem}{suffix}"
    return output_dir / filename


def metadata_path_for(output: Path) -> Path:
    return output.with_suffix(output.suffix + ".metadata.json")


def count_enabled_channels(channel_mask: int) -> int:
    return bin(channel_mask & 0xFFFFFFFF).count("1")


def safe_parse_response(response):
    if isinstance(response, dict):
        return response
    return ResponseParser.parse(response)


def build_daq_metrics(result, daq_status, *, requested_sample_rate: int, block_samples: int, channel_mask: int) -> dict:
    stream = safe_parse_response(result)
    status = safe_parse_response(daq_status)

    bytes_received = int(stream.get("bytes_received", 0) or 0)
    elapsed_seconds = float(stream.get("elapsed_seconds", 0.0) or 0.0)
    stream_frames = int(stream.get("frames_received", 0) or 0)
    if stream_frames == 0 and bytes_received > 0:
        stream_frames = bytes_received // DAQ_STREAM_FRAME_BYTES

    samples_captured = int(status.get("samples_captured", 0) or 0) if isinstance(status, dict) else 0
    dropped_buffers = int(status.get("dropped_buffers", 0) or 0) if isinstance(status, dict) else 0
    board_reported_bytes = int(status.get("bytes_written", 0) or 0) if isinstance(status, dict) else 0
    estimated_unreceived_frames = max(samples_captured - stream_frames, 0)
    estimated_unreceived_bytes = estimated_unreceived_frames * DAQ_STREAM_FRAME_BYTES
    estimated_dropped_frames_min = dropped_buffers * block_samples

    return {
        "requested_sample_rate_hz": requested_sample_rate,
        "channel_mask": channel_mask,
        "channel_mask_hex": f"0x{channel_mask:08X}",
        "enabled_channel_count": count_enabled_channels(channel_mask),
        "block_samples": block_samples,
        "stream_frame_bytes": DAQ_STREAM_FRAME_BYTES,
        "bytes_received": bytes_received,
        "bytes_received_mib": round(bytes_received / (1024.0 * 1024.0), 6),
        "elapsed_seconds": round(elapsed_seconds, 3),
        "stream_frames_received": stream_frames,
        "stream_frames_per_sec": round(stream_frames / elapsed_seconds, 3) if elapsed_seconds > 0.0 else 0.0,
        "stream_mib_per_sec": round((bytes_received / (1024.0 * 1024.0)) / elapsed_seconds, 6) if elapsed_seconds > 0.0 else 0.0,
        "partial_tail_bytes": bytes_received % DAQ_STREAM_FRAME_BYTES,
        "board_samples_captured": samples_captured,
        "board_capture_frames_per_sec": round(samples_captured / elapsed_seconds, 3) if elapsed_seconds > 0.0 else 0.0,
        "board_reported_bytes_written": board_reported_bytes,
        "board_reported_mib": round(board_reported_bytes / (1024.0 * 1024.0), 6),
        "dropped_buffers": dropped_buffers,
        "estimated_dropped_frames_min": estimated_dropped_frames_min,
        "estimated_dropped_bytes_min": estimated_dropped_frames_min * DAQ_STREAM_FRAME_BYTES,
        "estimated_unreceived_frames_vs_board_captured": estimated_unreceived_frames,
        "estimated_unreceived_bytes_vs_board_captured": estimated_unreceived_bytes,
        "receive_percent_of_board_captured": round((stream_frames / samples_captured) * 100.0, 3) if samples_captured > 0 else 0.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Start simultaneous streams from multiple boards listed by IP")
    parser.add_argument("--host", default="0.0.0.0", help="Host interface to listen on")
    parser.add_argument("--port", type=int, default=10, help="TCP port to listen on")
    parser.add_argument("--ip-file", required=True, help="Text file containing one board IP per line")
    parser.add_argument("--remote-file", default="daq.bin", help="Remote filename to stream from every board")
    parser.add_argument("--mode", choices=["file", "daq"], default="daq", help="Streaming mode for every board")
    parser.add_argument("--file-output-format", choices=["bin", "csv"], default="bin", help="For --mode file: write raw binary file or convert DAQ file to parsed CSV")
    parser.add_argument("--daq-output-format", choices=["csv", "bin"], default="csv", help="For --mode daq: write parsed channel CSV or raw binary stream")
    parser.add_argument("--output-dir", default="captures", help="Directory where host-side outputs are written")
    parser.add_argument("--timeout", type=float, default=60.0, help="Seconds to wait for each requested board to connect")
    parser.add_argument("--duration", type=float, default=30.0, help="For --mode daq: seconds to stream before command 12 stops every board")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ stream sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    ip_file = Path(args.ip_file)
    board_ips = load_board_ips(ip_file)
    if not board_ips:
        raise SystemExit(f"No board IPs found in {ip_file}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    server = BoardServer(host=args.host, port=args.port)
    server.listen()
    print(f"Listening on {args.host}:{args.port}")

    try:
        sessions = {}
        for board_ip in board_ips:
            # Resolve each requested board explicitly so the example only operates on the listed IPs.
            print(f"Waiting for board {board_ip}...")
            sessions[board_ip] = server.wait_for_board(board_ip, timeout=args.timeout)
            print(f"Connected: {board_ip}:{sessions[board_ip].addr[1]}")

        handles: dict[str, StreamHandle] = {}
        start_acks = {}
        stop_acks = {}
        daq_statuses = {}
        for board_ip, session in sessions.items():
            output_path = default_output_path(output_dir, board_ip, args.remote_file, args.mode, args.daq_output_format, args.file_output_format)
            print(f"Starting {args.mode} stream on {board_ip} -> {output_path}")

            if args.mode == "file":
                # File mode either preserves the raw bytes or stages a temporary binary file for CSV conversion.
                stream_target = output_path if args.file_output_format == "bin" else output_path.with_suffix(output_path.suffix + ".raw.bin")
                handles[board_ip] = session.stream_file_async(
                    filename=args.remote_file,
                    local_path=stream_target,
                    verify_pattern=False,
                )
                handles[board_ip]._final_output_path = output_path
                handles[board_ip]._stream_target = stream_target
            else:
                # DAQ CSV mode captures raw bytes first, then converts after stop.
                # This keeps the receive thread lightweight enough for simultaneous boards.
                stream_target = output_path if args.daq_output_format == "bin" else output_path.with_suffix(output_path.suffix + ".raw.bin")
                handles[board_ip] = session.start_daq_stream_async(
                    filename=args.remote_file,
                    sample_rate_hz=args.sample_rate,
                    channel_mask=args.channel_mask,
                    block_samples=args.block_samples,
                    csv_path=None,
                    local_path=stream_target,
                )
                handles[board_ip]._final_output_path = output_path
                handles[board_ip]._stream_target = stream_target

        if args.mode == "daq":
            for board_ip, session in sessions.items():
                try:
                    start_acks[board_ip] = session.wait_for_command(13, timeout=2.0)
                except TimeoutError:
                    start_acks[board_ip] = None

            print(f"Streaming DAQ on {len(handles)} board(s) for {args.duration:.1f}s")
            end = time.monotonic() + args.duration
            while True:
                remaining = end - time.monotonic()
                if remaining <= 0:
                    break
                print(f"  remaining: {remaining:5.1f}s", end="\r", flush=True)
                time.sleep(min(0.5, remaining))
            print(" " * 32, end="\r", flush=True)

            print("Stopping DAQ streams on all boards")
            for board_ip, session in sessions.items():
                try:
                    stop_acks[board_ip] = session.stop_daq(timeout=max(args.timeout, 10.0))
                except Exception as exc:
                    stop_acks[board_ip] = {"error": str(exc)}

        for board_ip, handle in handles.items():
            # Wait for each active stream to finish and report the per-board result.
            result = handle.wait(timeout=max(args.timeout, 30.0 if args.mode == "daq" else 300.0))
            if args.mode == "daq":
                try:
                    daq_statuses[board_ip] = sessions[board_ip].get_daq_status(log_status=False, timeout=max(args.timeout, 10.0))
                except Exception as exc:
                    daq_statuses[board_ip] = {"error": str(exc)}

            if args.mode == "file" and args.file_output_format == "csv":
                frames_written = convert_daq_bin_to_csv(handle._stream_target, handle._final_output_path)
                metadata = {
                    "board_ip": board_ip,
                    "remote_file": args.remote_file,
                    "output_file": str(handle._final_output_path),
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
                metadata_path = metadata_path_for(Path(handle._final_output_path))
                metadata_path.write_text(json.dumps(metadata, indent=2))
                Path(handle._stream_target).unlink(missing_ok=True)
                print({"board_ip": board_ip, "converted_csv": str(handle._final_output_path), "frames_written": frames_written})
                print({"board_ip": board_ip, "metadata": str(metadata_path)})
            elif args.mode == "daq":
                output_path = Path(handle._final_output_path)
                frames_written = None
                if args.daq_output_format == "csv":
                    frames_written = convert_daq_stream_bin_to_csv(handle._stream_target, handle._final_output_path)
                    Path(handle._stream_target).unlink(missing_ok=True)

                derived_metrics = build_daq_metrics(
                    result,
                    daq_statuses.get(board_ip),
                    requested_sample_rate=args.sample_rate,
                    block_samples=args.block_samples,
                    channel_mask=args.channel_mask,
                )
                metadata = {
                    "board_ip": board_ip,
                    "remote_file": args.remote_file,
                    "output_file": str(output_path),
                    "output_format": args.daq_output_format,
                    "duration_seconds": args.duration,
                    "requested_daq_config": {
                        "sample_rate_hz": args.sample_rate,
                        "channel_mask": args.channel_mask,
                        "channel_mask_hex": f"0x{args.channel_mask:08X}",
                        "block_samples": args.block_samples,
                        "enabled_channel_count": count_enabled_channels(args.channel_mask),
                    },
                    "command_13_ack_present": start_acks.get(board_ip) is not None,
                    "command_13_ack": ResponseParser.parse(start_acks.get(board_ip)),
                    "command_12_ack": ResponseParser.parse(stop_acks.get(board_ip)) if not isinstance(stop_acks.get(board_ip), dict) else stop_acks.get(board_ip),
                    "command_10_status": ResponseParser.parse(daq_statuses.get(board_ip)) if not isinstance(daq_statuses.get(board_ip), dict) else daq_statuses.get(board_ip),
                    "command_13_stream": ResponseParser.parse(result),
                    "frames_written": frames_written,
                    "derived_metrics": derived_metrics,
                }
                metadata_path = metadata_path_for(output_path)
                metadata_path.write_text(json.dumps(metadata, indent=2))
                print({"board_ip": board_ip, "metadata": str(metadata_path)})
                if frames_written is not None:
                    print({"board_ip": board_ip, "converted_csv": str(output_path), "frames_written": frames_written})
                print({"board_ip": board_ip, "derived_metrics": derived_metrics})
            print(ResponseParser.parse(result))
    finally:
        server.close()


if __name__ == "__main__":
    main()
