from __future__ import annotations

import argparse
import json
import os
import socket
import sys
from typing import Any

DEFAULT_SOCKET = os.environ.get("AEMS_API_SOCKET", "/run/aems-server/aems-boardd.sock")


def _request(socket_path: str, action: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    if not hasattr(socket, "AF_UNIX"):
        raise RuntimeError("aemsctl requires Unix domain sockets; run this CLI on the Raspberry Pi server")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(socket_path)
        payload = {"action": action, "params": params or {}}
        client.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        response = b""
        while not response.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                break
            response += chunk
    if not response:
        raise RuntimeError("daemon returned no response")
    return json.loads(response.decode("utf-8"))


def _print_response(response: dict[str, Any]) -> int:
    print(json.dumps(response, indent=2))
    return 0 if response.get("ok") else 1


def _parse_int(value: str) -> int:
    return int(value, 0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Control the Raspberry Pi AEMS board daemon")
    parser.add_argument("--socket", default=DEFAULT_SOCKET, help="Daemon Unix socket path")
    sub = parser.add_subparsers(dest="group", required=True)

    health = sub.add_parser("health", help="Show daemon/board health summary")
    health.add_argument("--poll", action="store_true", help="Poll boards before returning health")

    shadow = sub.add_parser("shadow", help="Print AWS IoT shadow-compatible reported state")
    shadow.add_argument("--poll", action="store_true", help="Poll boards before returning shadow state")

    server = sub.add_parser("server", help="Server daemon commands")
    server_sub = server.add_subparsers(dest="command", required=True)
    server_sub.add_parser("status", help="Show daemon status")

    boards = sub.add_parser("boards", help="List known boards")
    boards.add_argument("--active", action="store_true", help="Show only active TCP connections")

    jobs = sub.add_parser("jobs", help="List DAQ jobs")
    jobs.add_argument("--active", action="store_true", help="Show only running/stopping jobs")

    transfers = sub.add_parser("transfers", help="List transfer jobs")
    transfers.add_argument("--active", action="store_true", help="Show only running transfers")

    transfer = sub.add_parser("transfer", help="Start local or cloud transfer jobs")
    transfer_sub = transfer.add_subparsers(dest="command", required=True)
    p = transfer_sub.add_parser("start")
    p.add_argument("--target", choices=["local", "s3"], default="local")
    p.add_argument("--dest", default=None, help="Local destination folder for --target local")
    p.add_argument("--bucket", default=None, help="S3 bucket for --target s3")
    p.add_argument("--prefix", default=None, help="S3 key prefix or local logical prefix")

    schedules = sub.add_parser("schedules", help="List schedules")
    schedules.add_argument("--enabled", action="store_true", help="Show only enabled schedules")

    schedule = sub.add_parser("schedule", help="Manage autonomous schedules")
    schedule_sub = schedule.add_subparsers(dest="command", required=True)
    p = schedule_sub.add_parser("add")
    p.add_argument("name")
    p.add_argument("--board", default="all")
    p.add_argument("--mode", choices=["daq_stream", "daq_log"], default="daq_stream")
    p.add_argument("--start", required=True, dest="start_time", help="Local time HH:MM")
    p.add_argument("--duration", required=True, type=float, help="Run duration in seconds")
    p.add_argument("--file-template", default="daq_{board_ip}_{date}.bin")
    p.add_argument("--format", choices=["bin", "csv"], default="bin", help="Host stream output format")
    p.add_argument("--sample-rate", type=int, default=2000)
    p.add_argument("--channel-mask", type=_parse_int, default=0x3F)
    p.add_argument("--block-samples", type=int, default=128)
    p = schedule_sub.add_parser("remove")
    p.add_argument("name")
    p = schedule_sub.add_parser("enable")
    p.add_argument("name")
    p = schedule_sub.add_parser("disable")
    p.add_argument("name")

    board = sub.add_parser("board", help="Single-board commands")
    board.add_argument("ip", help="Board IP address")
    board_sub = board.add_subparsers(dest="command", required=True)
    board_sub.add_parser("heartbeat", help="Send command 0")
    board_sub.add_parser("openamp", help="Send command 99")
    board_sub.add_parser("status", help="Send command 10")

    emmc = sub.add_parser("emmc", help="eMMC file commands")
    emmc_sub = emmc.add_subparsers(dest="command", required=True)
    for name in ("list", "delete-logs"):
        p = emmc_sub.add_parser(name)
        p.add_argument("--board", default="all", help="Board IP or all")
    p = emmc_sub.add_parser("size")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--file", required=True, help="Remote filename")
    p = emmc_sub.add_parser("delete")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--file", required=True, help="Remote filename")

    daq = sub.add_parser("daq", help="DAQ commands")
    daq_sub = daq.add_subparsers(dest="command", required=True)
    p = daq_sub.add_parser("status")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--log-status", action="store_true", help="Use command 110 instead of command 10")

    stream = daq_sub.add_parser("stream")
    stream_sub = stream.add_subparsers(dest="stream_command", required=True)
    p = stream_sub.add_parser("start")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--file", default="daq.bin", help="DAQ filename sent to firmware")
    p.add_argument("--format", choices=["bin", "csv"], default="bin", help="Host output format")
    p.add_argument("--output-dir", default=None, help="Host capture directory")
    p.add_argument("--duration", type=float, default=None, help="Optional seconds before automatic stop")
    p.add_argument("--sample-rate", type=int, default=2000)
    p.add_argument("--channel-mask", type=_parse_int, default=0x3F)
    p.add_argument("--block-samples", type=int, default=128)
    p.add_argument("--timeout", type=float, default=30.0)
    p = stream_sub.add_parser("stop")
    p.add_argument("--board", default=None, help="Stop jobs for this board IP")
    p.add_argument("--job-id", default=None, help="Stop one job ID")

    log = daq_sub.add_parser("log")
    log_sub = log.add_subparsers(dest="log_command", required=True)
    p = log_sub.add_parser("start")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--file", default="daq.bin")
    p.add_argument("--sample-rate", type=int, default=2000)
    p.add_argument("--channel-mask", type=_parse_int, default=0x3F)
    p.add_argument("--block-samples", type=int, default=128)
    p.add_argument("--stream-samples", type=int, default=0)
    p.add_argument("--timeout", type=float, default=5.0)
    p = log_sub.add_parser("stop")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--timeout", type=float, default=10.0)
    p = log_sub.add_parser("run")
    p.add_argument("--board", default="all", help="Board IP or all")
    p.add_argument("--file", default="daq_{board_ip}_{date}.bin")
    p.add_argument("--duration", required=True, type=float)
    p.add_argument("--sample-rate", type=int, default=2000)
    p.add_argument("--channel-mask", type=_parse_int, default=0x3F)
    p.add_argument("--block-samples", type=int, default=128)
    p.add_argument("--timeout", type=float, default=30.0)

    cal = sub.add_parser("calibration", help="Offset calibration commands")
    cal_sub = cal.add_subparsers(dest="command", required=True)
    for name in ("get", "run"):
        p = cal_sub.add_parser(name)
        p.add_argument("--board", default="all", help="Board IP or all")

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    action = ""
    params: dict[str, Any] = {}

    if args.group == "server" and args.command == "status":
        action = "server.status"
    elif args.group == "health":
        action = "health.get"
        params = {"poll": args.poll}
    elif args.group == "shadow":
        action = "health.shadow"
        params = {"poll": args.poll}
    elif args.group == "boards":
        action = "boards.list"
        params = {"active": True if args.active else None}
    elif args.group == "jobs":
        action = "jobs.list"
        params = {"active": True if args.active else None}
    elif args.group == "transfers":
        action = "transfers.list"
        params = {"active": True if args.active else None}
    elif args.group == "transfer":
        action = "transfer.start"
        params = {"target": args.target, "dest": args.dest, "bucket": args.bucket, "prefix": args.prefix}
    elif args.group == "schedules":
        action = "schedules.list"
        params = {"enabled": True if args.enabled else None}
    elif args.group == "schedule":
        if args.command == "add":
            action = "schedule.add"
            params = {
                "name": args.name,
                "board": args.board,
                "mode": args.mode,
                "start_time": args.start_time,
                "duration": args.duration,
                "file_template": args.file_template,
                "format": args.format,
                "sample_rate": args.sample_rate,
                "channel_mask": args.channel_mask,
                "block_samples": args.block_samples,
            }
        elif args.command == "remove":
            action = "schedule.remove"
            params = {"name": args.name}
        else:
            action = "schedule.enable"
            params = {"name": args.name, "enabled": args.command == "enable"}
    elif args.group == "board":
        action = {"heartbeat": "board.heartbeat", "openamp": "board.openamp", "status": "board.status"}[args.command]
        params = {"board": args.ip}
    elif args.group == "emmc":
        action = {"list": "emmc.list", "size": "emmc.size", "delete": "emmc.delete", "delete-logs": "emmc.delete_logs"}[args.command]
        params = {"board": args.board}
        if hasattr(args, "file"):
            params["file"] = args.file
    elif args.group == "daq" and args.command == "status":
        action = "daq.status"
        params = {"board": args.board, "log_status": args.log_status}
    elif args.group == "daq" and args.command == "stream":
        if args.stream_command == "start":
            action = "daq.stream.start"
            params = {
                "board": args.board,
                "file": args.file,
                "format": args.format,
                "output_dir": args.output_dir,
                "duration": args.duration,
                "sample_rate": args.sample_rate,
                "channel_mask": args.channel_mask,
                "block_samples": args.block_samples,
                "timeout": args.timeout,
            }
        else:
            action = "daq.stream.stop"
            params = {"board": args.board, "job_id": args.job_id}
    elif args.group == "daq" and args.command == "log":
        if args.log_command == "start":
            action = "daq.log.start"
            params = {
                "board": args.board,
                "file": args.file,
                "sample_rate": args.sample_rate,
                "channel_mask": args.channel_mask,
                "block_samples": args.block_samples,
                "stream_samples": args.stream_samples,
                "timeout": args.timeout,
            }
        elif args.log_command == "stop":
            action = "daq.log.stop"
            params = {"board": args.board, "timeout": args.timeout}
        else:
            action = "daq.log.run"
            params = {
                "board": args.board,
                "file": args.file,
                "duration": args.duration,
                "sample_rate": args.sample_rate,
                "channel_mask": args.channel_mask,
                "block_samples": args.block_samples,
                "timeout": args.timeout,
            }
    elif args.group == "calibration":
        action = "calibration.get" if args.command == "get" else "calibration.run"
        params = {"board": args.board}

    try:
        response = _request(args.socket, action, params)
    except Exception as exc:
        print(f"aemsctl: {exc}", file=sys.stderr)
        sys.exit(2)
    sys.exit(_print_response(response))


if __name__ == "__main__":
    main()
