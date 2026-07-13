from __future__ import annotations

import argparse
import json
from pathlib import Path

from board_interface import BoardServer, ResponseParser


def load_board_ips(path: Path) -> list[str]:
    # Each non-empty, non-comment line is treated as one target board IP.
    ips: list[str] = []
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        ips.append(line)
    return ips


def main() -> None:
    parser = argparse.ArgumentParser(description="Run DAQ logging on multiple boards listed by IP")
    parser.add_argument("--host", default="0.0.0.0", help="Host interface to listen on")
    parser.add_argument("--port", type=int, default=10, help="TCP port to listen on")
    parser.add_argument("--ip-file", required=True, help="Text file containing one board IP per line")
    parser.add_argument("--filename-prefix", default="daq", help="Remote filename prefix for each board")
    parser.add_argument("--duration", type=float, default=15.0, help="Seconds to log before stopping")
    parser.add_argument("--timeout", type=float, default=60.0, help="Seconds to wait for each requested board to connect")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    board_ips = load_board_ips(Path(args.ip_file))
    if not board_ips:
        raise SystemExit(f"No board IPs found in {args.ip_file}")

    server = BoardServer(host=args.host, port=args.port)
    server.listen()
    print(f"Listening on {args.host}:{args.port}")

    try:
        sessions = {}
        filenames = {}
        for board_ip in board_ips:
            # Resolve only the boards listed in the IP file.
            print(f"Waiting for board {board_ip}...")
            session = server.wait_for_board(board_ip, timeout=args.timeout)
            sessions[board_ip] = session
            safe_ip = board_ip.replace('.', '_')
            filenames[board_ip] = f"{args.filename_prefix}_{safe_ip}.bin"
            print(f"Connected: {board_ip}:{session.addr[1]}")

        # Start logging on every connected board first so acquisition windows overlap.
        for board_ip, session in sessions.items():
            remote_filename = filenames[board_ip]
            print(f"Starting DAQ log on {board_ip} -> {remote_filename}")
            ack = session.start_daq_log(
                filename=remote_filename,
                sample_rate_hz=args.sample_rate,
                channel_mask=args.channel_mask,
                block_samples=args.block_samples,
                timeout=max(args.timeout, 10.0),
            )
            print(json.dumps(ResponseParser.parse(ack), indent=2))

        print(f"Logging on {len(sessions)} boards for {args.duration:.1f}s")
        import time
        time.sleep(args.duration)

        # Stop each board and capture resulting metrics and file size.
        for board_ip, session in sessions.items():
            remote_filename = filenames[board_ip]
            print(f"Stopping DAQ log on {board_ip}")
            stop_ack = session.stop_daq_log(timeout=max(args.timeout, 10.0))
            print(json.dumps(ResponseParser.parse(stop_ack), indent=2))

            status = session.get_daq_status(log_status=True, timeout=max(args.timeout, 10.0))
            print(json.dumps(ResponseParser.parse(status), indent=2))

            file_size = session.get_file_size(remote_filename, timeout=max(args.timeout, 10.0))
            print(json.dumps(ResponseParser.parse(file_size), indent=2))
    finally:
        server.close()


if __name__ == "__main__":
    main()
