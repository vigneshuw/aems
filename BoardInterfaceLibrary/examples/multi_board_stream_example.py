from __future__ import annotations

import argparse
from pathlib import Path

from board_interface import BoardServer, ResponseParser, StreamHandle


def load_board_ips(path: Path) -> list[str]:
    # Each non-empty, non-comment line is treated as one target board IP.
    ips: list[str] = []
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        ips.append(line)
    return ips


def default_output_path(output_dir: Path, board_ip: str, remote_file: str, mode: str, daq_output_format: str) -> Path:
    if mode == "daq":
        suffix = ".csv" if daq_output_format == "csv" else ".bin"
        filename = f"{board_ip}_{Path(remote_file).stem}{suffix}"
    else:
        suffix = Path(remote_file).suffix or ".bin"
        filename = f"{board_ip}_{Path(remote_file).stem}{suffix}"
    return output_dir / filename


def main() -> None:
    parser = argparse.ArgumentParser(description="Start simultaneous streams from multiple boards listed by IP")
    parser.add_argument("--host", default="0.0.0.0", help="Host interface to listen on")
    parser.add_argument("--port", type=int, default=10, help="TCP port to listen on")
    parser.add_argument("--ip-file", required=True, help="Text file containing one board IP per line")
    parser.add_argument("--remote-file", default="daq.bin", help="Remote filename to stream from every board")
    parser.add_argument("--mode", choices=["file", "daq"], default="daq", help="Streaming mode for every board")
    parser.add_argument("--daq-output-format", choices=["csv", "bin"], default="csv", help="For --mode daq: write parsed channel CSV or raw binary stream")
    parser.add_argument("--output-dir", default="captures", help="Directory where host-side outputs are written")
    parser.add_argument("--timeout", type=float, default=60.0, help="Seconds to wait for each requested board to connect")
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
        for board_ip, session in sessions.items():
            output_path = default_output_path(output_dir, board_ip, args.remote_file, args.mode, args.daq_output_format)
            print(f"Starting {args.mode} stream on {board_ip} -> {output_path}")

            if args.mode == "file":
                # File mode writes the remote file directly to a local binary file.
                handles[board_ip] = session.stream_file_async(
                    filename=args.remote_file,
                    local_path=output_path,
                    verify_pattern=False,
                )
            else:
                # DAQ mode writes decoded frames into one CSV per board.
                handles[board_ip] = session.start_daq_stream_async(
                    filename=args.remote_file,
                    sample_rate_hz=args.sample_rate,
                    channel_mask=args.channel_mask,
                    block_samples=args.block_samples,
                    csv_path=output_path if args.daq_output_format == "csv" else None,
                    local_path=output_path if args.daq_output_format == "bin" else None,
                )

        for board_ip, handle in handles.items():
            # Wait for each active stream to finish and report the per-board result.
            result = handle.wait(timeout=max(args.timeout, 300.0))
            print(ResponseParser.parse(result))
    finally:
        server.close()


if __name__ == "__main__":
    main()
