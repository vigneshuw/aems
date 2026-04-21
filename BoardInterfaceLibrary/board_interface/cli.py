from __future__ import annotations

import argparse
from dataclasses import asdict, is_dataclass
from pprint import pprint

from .client import BoardServer


def _show(value: object) -> None:
    if is_dataclass(value):
        pprint(asdict(value))
    else:
        print(value)


def interactive(host: str, port: int) -> None:
    server = BoardServer(host=host, port=port)
    server.listen()
    print(f"Listening on {host}:{port}")
    print("Waiting for board connection...")
    session = server.accept()
    print(f"Board connected from {session.addr[0]}:{session.addr[1]}")

    prompt = (
        "Enter command (0=heartbeat, 1=write config, 2=dat count, 3=all file count, 4=file list, "
        "5=file size [asks filename], 6=delete .bin/.dat, 7=delete file [asks filename], 8=stream file, 9=test stream, 10=daq status, "
        "11=daq log [asks filename], 12=daq stop, 13=daq stream [asks filename], "
        "110=daq log status, 112=daq log stop/close, 99=openamp heartbeat, q=quit): "
    )

    try:
        while True:
            command = input(prompt).strip().lower()
            if command == "q":
                break
            if command == "0":
                _show(session.heartbeat())
            elif command == "1":
                _show(session.write_config())
            elif command == "2":
                _show(session.get_dat_file_count())
            elif command == "3":
                _show(session.get_all_file_count())
            elif command == "4":
                response = session.get_file_list(timeout=10.0)
                print(f"File list: {len(response.entries)} file(s)")
                for entry in response.entries:
                    print(f" - {entry.name} ({entry.size} bytes)")
            elif command == "5":
                default_filename = session.last_daq_filename or session.default_config.read_filename
                filename = input(f"Filename [{default_filename}]: ").strip()
                _show(session.get_file_size(filename or default_filename))
            elif command == "6":
                _show(session.delete_log_files())
            elif command == "7":
                filename = input("Delete filename: ").strip()
                if not filename:
                    print("Filename required.")
                else:
                    _show(session.delete_file(filename))
            elif command == "8":
                filename = input(f"Filename [{session.default_config.read_filename}]: ").strip()
                offset = int((input("Offset [0]: ").strip() or "0"), 0)
                _show(session.stream_file(filename or None, offset, timeout=30.0))
            elif command == "9":
                _show(session.test_stream(timeout=30.0))
            elif command == "10":
                _show(session.get_daq_status(log_status=False))
            elif command == "11":
                filename = input(f"DAQ filename [{session.default_config.daq_filename}]: ").strip()
                sample_rate = input(f"Sample rate [{session.default_config.daq_sample_rate_hz}]: ").strip()
                channel_mask = input(f"Channel mask [0x{session.default_config.daq_channel_mask:02X}]: ").strip()
                block_samples = input(f"Block samples [{session.default_config.daq_block_samples}]: ").strip()
                _show(session.start_daq_log(
                    filename=filename or session.default_config.daq_filename,
                    sample_rate_hz=int(sample_rate or session.default_config.daq_sample_rate_hz),
                    channel_mask=int(channel_mask, 0) if channel_mask else session.default_config.daq_channel_mask,
                    block_samples=int(block_samples or session.default_config.daq_block_samples),
                ))
            elif command == "12":
                _show(session.stop_daq())
            elif command == "13":
                filename = input(f"DAQ filename [{session.default_config.daq_filename}]: ").strip()
                csv_path = input("CSV path [stream_data.csv]: ").strip() or "stream_data.csv"
                _show(session.start_daq_stream(filename=filename or session.default_config.daq_filename, csv_path=csv_path, timeout=30.0))
            elif command == "110":
                _show(session.get_daq_status(log_status=True))
            elif command == "112":
                _show(session.stop_daq_log())
            elif command == "99":
                _show(session.openamp_heartbeat())
            else:
                print("Unsupported command.")
    finally:
        session.close()
        server.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Interactive CLI for the board interface library")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=10)
    args = parser.parse_args()
    interactive(args.host, args.port)


if __name__ == "__main__":
    main()
