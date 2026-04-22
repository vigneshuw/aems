from __future__ import annotations

from common import build_parser, open_server, sleep_with_progress, wait_for_session


def main() -> None:
    parser = build_parser("Run DAQ logging on a board, stop it, and query the resulting file size")
    parser.add_argument("--filename", default="daq.bin", help="Remote DAQ log filename")
    parser.add_argument("--duration", type=float, default=10.0, help="Seconds to log before stopping")
    parser.add_argument("--sample-rate", type=int, default=2000, help="DAQ sample rate")
    parser.add_argument("--channel-mask", type=lambda value: int(value, 0), default=0x3F, help="DAQ channel mask")
    parser.add_argument("--block-samples", type=int, default=128, help="DAQ block size")
    args = parser.parse_args()

    server = open_server(args.host, args.port)
    try:
        session = wait_for_session(server, args.board_ip, args.timeout)

        # Start logging on the board. The file is created remotely on the board eMMC.
        print(f"Starting DAQ log to {args.filename}")
        start_ack = session.start_daq_log(
            filename=args.filename,
            sample_rate_hz=args.sample_rate,
            channel_mask=args.channel_mask,
            block_samples=args.block_samples,
            timeout=max(args.timeout, 10.0),
        )
        print(start_ack)

        # Let acquisition run for the requested dwell time.
        print(f"Logging for {args.duration:.1f}s")
        sleep_with_progress(args.duration)

        # Stop the logger and then ask the board for its final status counters.
        print("Stopping DAQ log")
        stop_ack = session.stop_daq_log(timeout=max(args.timeout, 10.0))
        print(stop_ack)

        print("Reading DAQ status")
        status = session.get_daq_status(log_status=True, timeout=max(args.timeout, 10.0))
        print(status)

        # Finally confirm the remote file exists and has the expected size.
        print(f"Reading file size for {args.filename}")
        file_size = session.get_file_size(args.filename, timeout=max(args.timeout, 10.0))
        print(file_size)
    finally:
        server.close()


if __name__ == "__main__":
    main()
