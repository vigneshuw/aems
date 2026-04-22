from __future__ import annotations

from common import build_parser, open_server, wait_for_session


def main() -> None:
    parser = build_parser("Exercise the non-streaming board commands against one board")
    parser.add_argument("--read-file", default="test.dat", help="Filename for command 5")
    parser.add_argument("--delete-file", help="Optional filename for command 7")
    args = parser.parse_args()

    server = open_server(args.host, args.port)
    try:
        session = wait_for_session(server, args.board_ip, args.timeout)

        # Run a representative command sweep so library users have one place to copy from.
        print("Heartbeat")
        print(session.heartbeat())

        print("Write config")
        print(session.write_config())

        print("Count .dat files")
        print(session.get_dat_file_count())

        print("Count all files")
        print(session.get_all_file_count())

        print("List files")
        file_list = session.get_file_list(timeout=max(args.timeout, 10.0))
        for entry in file_list.entries:
            print(f"  {entry.name} ({entry.size} bytes)")

        print(f"Get file size for {args.read_file}")
        print(session.get_file_size(args.read_file, timeout=max(args.timeout, 10.0)))

        print("DAQ status")
        print(session.get_daq_status(timeout=max(args.timeout, 10.0)))

        print("DAQ log status")
        print(session.get_daq_status(log_status=True, timeout=max(args.timeout, 10.0)))

        print("OpenAMP heartbeat")
        print(session.openamp_heartbeat(timeout=max(args.timeout, 10.0)))

        # Optional targeted delete first, then broad cleanup of .bin/.dat logs.
        if args.delete_file:
            print(f"Delete file {args.delete_file}")
            print(session.delete_file(args.delete_file, timeout=max(args.timeout, 10.0)))

        print("Delete all .bin/.dat files")
        print(session.delete_log_files(timeout=max(args.timeout, 10.0)))
    finally:
        server.close()


if __name__ == "__main__":
    main()
