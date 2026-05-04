from __future__ import annotations

import threading
import time
import uuid
import shutil
from datetime import datetime
from pathlib import Path
from typing import Any, Callable

from ..response_parser import ResponseParser
from ..server import BoardServer
from ..session import BoardSession
from .config import DaemonConfig
from .database import AemsDatabase
from .jobs import JobManager


class AemsManager:
    """Owns the board TCP listener and exposes safe operations for the local API."""

    def __init__(self, *, config: DaemonConfig, database: AemsDatabase) -> None:
        self.config = config
        self.database = database
        self.server = BoardServer(host=config.host, port=config.port, backlog=config.backlog)
        self.jobs = JobManager(
            database=database,
            session_getter=self.get_session,
            data_dir=config.data_dir,
            capture_dir=config.capture_dir,
            metadata_dir=config.metadata_dir,
            manifest_dir=config.manifest_dir,
            site_id=config.site_id,
            pi_id=config.pi_id,
            transfer_retry_limit=config.transfer_retry_limit,
        )
        self.started_at = time.time()
        self._stop_event = threading.Event()
        self._accept_thread: threading.Thread | None = None
        self._poll_thread: threading.Thread | None = None
        self._health_thread: threading.Thread | None = None
        self._schedule_thread: threading.Thread | None = None
        self._active_ips: set[str] = set()

    def start(self) -> None:
        self.server.listen()
        self._accept_thread = threading.Thread(target=self._accept_loop, name="AemsAcceptMonitor", daemon=True)
        self._poll_thread = threading.Thread(target=self._poll_loop, name="AemsBoardPoll", daemon=True)
        self._health_thread = threading.Thread(target=self._health_loop, name="AemsHealthPoll", daemon=True)
        self._schedule_thread = threading.Thread(target=self._schedule_loop, name="AemsScheduler", daemon=True)
        self._accept_thread.start()
        self._poll_thread.start()
        self._health_thread.start()
        self._schedule_thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        self.server.close()
        for ip in list(self._active_ips):
            self.database.mark_board_disconnected(ip, "daemon stopped")
        self._active_ips.clear()

    def get_session(self, board_ip: str) -> BoardSession:
        return self.server.get_board(board_ip)

    def handle_request(self, request: dict[str, Any]) -> dict[str, Any]:
        """Dispatch one JSON API request from aemsctl."""

        action = str(request.get("action", ""))
        params = request.get("params") or {}
        try:
            data = self._dispatch(action, params)
            return {"ok": True, "data": ResponseParser.parse(data)}
        except Exception as exc:
            return {"ok": False, "error": str(exc), "action": action}

    def _dispatch(self, action: str, params: dict[str, Any]) -> Any:
        if action == "server.status":
            self._sync_active_boards()
            return self.server_status()
        if action == "health.get":
            poll = bool(params.get("poll", False))
            if poll:
                self._poll_board_health()
            health = self.build_health_snapshot()
            self.database.save_health_snapshot(health)
            return health
        if action == "health.shadow":
            poll = bool(params.get("poll", False))
            if poll:
                self._poll_board_health()
            return self.build_shadow_document()
        if action == "boards.list":
            self._sync_active_boards()
            return self.database.list_boards(active=params.get("active"))
        if action == "jobs.list":
            return self.jobs.list_jobs(active=params.get("active"))
        if action == "transfers.list":
            return self.jobs.list_transfers(active=params.get("active"))
        if action == "transfer.start":
            target = str(params.get("target", "local"))
            return self.jobs.start_transfer(
                target=target,
                local_dest=Path(params["dest"]) if params.get("dest") else (self.config.data_dir / "transfer_out" if target == "local" else None),
                bucket=str(params.get("bucket")) if params.get("bucket") else (self.config.aws_s3_bucket or None),
                prefix=str(params.get("prefix")) if params.get("prefix") else self.config.aws_s3_prefix,
            )
        if action == "schedules.list":
            return self.database.list_schedules(enabled=params.get("enabled"))
        if action == "schedule.add":
            data = {
                "name": str(params["name"]),
                "enabled": bool(params.get("enabled", True)),
                "board": str(params.get("board", "all")),
                "mode": str(params.get("mode", "daq_stream")),
                "start_time": str(params["start_time"]),
                "duration_s": float(params["duration"]),
                "file_template": str(params.get("file_template", "daq_{board_ip}_{date}.bin")),
                "output_format": str(params.get("format", "bin")),
                "sample_rate_hz": int(params.get("sample_rate", 2000)),
                "channel_mask": int(params.get("channel_mask", 0x3F)),
                "block_samples": int(params.get("block_samples", 128)),
            }
            return self.database.upsert_schedule(uuid.uuid4().hex, data)
        if action == "schedule.remove":
            return {"removed": self.database.delete_schedule(str(params["name"]))}
        if action == "schedule.enable":
            return {"updated": self.database.set_schedule_enabled(str(params["name"]), bool(params.get("enabled", True)))}
        if action == "daq.stream.stop":
            board = params.get("board")
            return self.jobs.stop(job_id=params.get("job_id"), board_ip=None if board in (None, "all") else str(board))
        if action == "daq.stream.start":
            return self._run_for_targets(
                str(params.get("board", "all")),
                lambda ip: self.jobs.start_daq_stream(
                    board_ip=ip,
                    remote_file=str(params.get("file", "daq.bin")),
                    output_format=str(params.get("format", "bin")),
                    output_dir=Path(params["output_dir"]) if params.get("output_dir") else None,
                    duration_s=float(params["duration"]) if params.get("duration") is not None else None,
                    sample_rate_hz=int(params.get("sample_rate", 2000)),
                    channel_mask=int(params.get("channel_mask", 0x3F)),
                    block_samples=int(params.get("block_samples", 128)),
                    timeout_s=float(params.get("timeout", 30.0)),
                ),
            )
        if action == "daq.log.run":
            return self._run_for_targets(
                str(params.get("board", "all")),
                lambda ip: self.jobs.start_daq_log_run(
                    board_ip=ip,
                    remote_file=self._format_file_template(str(params.get("file", "daq_{board_ip}_{date}.bin")), ip),
                    duration_s=float(params["duration"]),
                    sample_rate_hz=int(params.get("sample_rate", 2000)),
                    channel_mask=int(params.get("channel_mask", 0x3F)),
                    block_samples=int(params.get("block_samples", 128)),
                    timeout_s=float(params.get("timeout", 30.0)),
                ),
            )

        board = str(params.get("board", "all"))
        timeout = float(params.get("timeout", 5.0))
        if action == "board.heartbeat":
            return self._run_for_targets(board, lambda ip: self._record(ip, "heartbeat", self.get_session(ip).heartbeat(timeout=timeout)))
        if action == "board.openamp":
            return self._run_for_targets(board, lambda ip: self._record(ip, "openamp", self.get_session(ip).openamp_heartbeat(timeout=timeout)))
        if action in {"board.status", "daq.status"}:
            return self._run_for_targets(
                board,
                lambda ip: self._record(
                    ip,
                    "status",
                    self.get_session(ip).get_daq_status(log_status=bool(params.get("log_status", False)), timeout=timeout),
                ),
            )
        if action == "emmc.list":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).get_file_list(timeout=float(params.get("timeout", 10.0))))
        if action == "emmc.size":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).get_file_size(str(params.get("file", "daq.bin")), timeout=timeout))
        if action == "emmc.delete":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).delete_file(str(params.get("file", "daq.bin")), timeout=timeout))
        if action == "emmc.delete_logs":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).delete_log_files(timeout=timeout))
        if action == "daq.log.start":
            return self._run_for_targets(
                board,
                lambda ip: self.get_session(ip).start_daq_log(
                    filename=str(params.get("file", "daq.bin")),
                    sample_rate_hz=int(params.get("sample_rate", 2000)),
                    channel_mask=int(params.get("channel_mask", 0x3F)),
                    block_samples=int(params.get("block_samples", 128)),
                    stream_samples=int(params.get("stream_samples", 0)),
                    timeout=timeout,
                ),
            )
        if action == "daq.log.stop":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).stop_daq_log(timeout=float(params.get("timeout", 10.0))))
        if action == "calibration.get":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).get_offset_calibration(timeout=timeout))
        if action == "calibration.run":
            return self._run_for_targets(board, lambda ip: self.get_session(ip).run_offset_calibration(timeout=float(params.get("timeout", 30.0))))
        raise ValueError(f"Unsupported action: {action}")

    def server_status(self) -> dict[str, Any]:
        known = self.database.list_boards(active=None)
        active = [row for row in known if row["active"]]
        return {
            "host": self.config.host,
            "port": self.config.port,
            "site_id": self.config.site_id,
            "pi_id": self.config.pi_id,
            "api_socket": str(self.config.api_socket),
            "database_path": str(self.config.database_path),
            "capture_dir": str(self.config.capture_dir),
            "metadata_dir": str(self.config.metadata_dir),
            "manifest_dir": str(self.config.manifest_dir),
            "uptime_seconds": round(time.time() - self.started_at, 3),
            "known_board_count": len(known),
            "active_board_count": len(active),
        }

    def _record(self, ip_address: str, response_kind: str, response: Any) -> Any:
        self.database.update_board_response(ip_address, response_kind, response)
        return response

    def _run_for_targets(self, target: str, operation: Callable[[str], Any]) -> Any:
        targets = self._target_ips(target)
        if target != "all" and len(targets) == 1:
            return operation(targets[0])
        result: dict[str, Any] = {}
        for ip in targets:
            try:
                result[ip] = operation(ip)
            except Exception as exc:
                result[ip] = {"error": str(exc)}
        return result

    def _target_ips(self, target: str) -> list[str]:
        self._sync_active_boards()
        if target == "all":
            return [row["ip_address"] for row in self.database.list_boards(active=True)]
        # Validate that the requested board is currently reachable.
        self.get_session(target)
        return [target]

    def _accept_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                session = self.server.accept(timeout=self.config.accept_timeout_s)
            except TimeoutError:
                continue
            except OSError:
                break
            info = session.snapshot()
            self.database.mark_board_connected(info)
            self._active_ips.add(info.ip_address)

    def _poll_loop(self) -> None:
        while not self._stop_event.is_set():
            self._sync_active_boards()
            self._stop_event.wait(self.config.poll_interval_s)

    def _health_loop(self) -> None:
        while not self._stop_event.is_set():
            self._poll_board_health()
            health = self.build_health_snapshot()
            self.database.save_health_snapshot(health)
            self._stop_event.wait(self.config.health_poll_interval_s)

    def _schedule_loop(self) -> None:
        while not self._stop_event.is_set():
            self._run_due_schedules()
            self._stop_event.wait(self.config.schedule_poll_interval_s)

    def _sync_active_boards(self) -> None:
        boards = self.server.list_boards()
        current = {board.ip_address for board in boards}
        for board in boards:
            self.database.mark_board_seen(board)
        for stale_ip in list(self._active_ips - current):
            self.database.mark_board_disconnected(stale_ip, "connection lost")
        self._active_ips = current

    def _poll_board_health(self) -> None:
        self._sync_active_boards()
        busy_boards = self.jobs.active_board_ips()
        for ip in list(self._active_ips):
            # Current stream framing cannot safely interleave arbitrary fixed
            # responses with command-13 payload. Skip polling while daemon jobs
            # own the DAQ stream/log control path for this board.
            if ip in busy_boards:
                continue
            try:
                heartbeat = self.get_session(ip).heartbeat(timeout=2.0)
                self.database.update_board_response(ip, "heartbeat", heartbeat)
            except Exception:
                pass
            try:
                openamp = self.get_session(ip).openamp_heartbeat(timeout=3.0)
                self.database.update_board_response(ip, "openamp", openamp)
            except Exception:
                pass
            try:
                status = self.get_session(ip).get_daq_status(log_status=False, timeout=3.0)
                self.database.update_board_response(ip, "status", status)
            except Exception:
                pass

    def build_health_snapshot(self) -> dict[str, Any]:
        self._sync_active_boards()
        disk = shutil.disk_usage(self.config.data_dir)
        boards = self.database.list_boards(active=None)
        return {
            "site_id": self.config.site_id,
            "pi_id": self.config.pi_id,
            "timestamp": time.time(),
            "server": self.server_status(),
            "storage": {
                "path": str(self.config.data_dir),
                "total_bytes": disk.total,
                "used_bytes": disk.used,
                "free_bytes": disk.free,
                "free_percent": round((disk.free / disk.total) * 100.0, 3) if disk.total else 0.0,
            },
            "boards": {row["ip_address"]: row for row in boards},
            "jobs": {
                "active": self.jobs.list_jobs(active=True),
                "recent": self.jobs.list_jobs(active=False)[:10],
            },
            "transfers": {
                "active": self.jobs.list_transfers(active=True),
                "recent": self.jobs.list_transfers(active=False)[:10],
            },
            "schedules": self.database.list_schedules(enabled=None),
        }

    def build_shadow_document(self) -> dict[str, Any]:
        health = self.build_health_snapshot()
        return {
            "state": {
                "reported": {
                    "site_id": self.config.site_id,
                    "pi_id": self.config.pi_id,
                    "server": health["server"],
                    "storage": health["storage"],
                    "boards": health["boards"],
                    "jobs": health["jobs"],
                    "transfers": health["transfers"],
                    "schedules": health["schedules"],
                }
            }
        }

    def _run_due_schedules(self) -> None:
        now = datetime.now()
        today = now.date().isoformat()
        current_hhmm = now.strftime("%H:%M")
        for schedule in self.database.list_schedules(enabled=True):
            if schedule["last_run_date"] == today:
                continue
            if current_hhmm < schedule["start_time"]:
                continue
            self.database.mark_schedule_run(schedule["schedule_id"], today)
            self._start_schedule(schedule)

    def _start_schedule(self, schedule: dict[str, Any]) -> None:
        mode = schedule["mode"]
        board = schedule["board"]
        if mode in {"daq_stream", "stream"}:
            self._run_for_targets(
                board,
                lambda ip: self.jobs.start_daq_stream(
                    board_ip=ip,
                    remote_file=self._format_file_template(schedule["file_template"], ip),
                    output_format=schedule["output_format"],
                    output_dir=None,
                    duration_s=float(schedule["duration_s"]),
                    sample_rate_hz=int(schedule["sample_rate_hz"]),
                    channel_mask=int(schedule["channel_mask"]),
                    block_samples=int(schedule["block_samples"]),
                    timeout_s=30.0,
                ),
            )
        elif mode in {"daq_log", "emmc_log", "log"}:
            self._run_for_targets(
                board,
                lambda ip: self.jobs.start_daq_log_run(
                    board_ip=ip,
                    remote_file=self._format_file_template(schedule["file_template"], ip),
                    duration_s=float(schedule["duration_s"]),
                    sample_rate_hz=int(schedule["sample_rate_hz"]),
                    channel_mask=int(schedule["channel_mask"]),
                    block_samples=int(schedule["block_samples"]),
                    timeout_s=30.0,
                ),
            )
        else:
            raise ValueError(f"Unsupported schedule mode: {mode}")

    def _format_file_template(self, template: str, board_ip: str) -> str:
        now = datetime.now()
        return template.format(
            board_ip=board_ip,
            board=board_ip.replace(".", "_"),
            site_id=self.config.site_id,
            pi_id=self.config.pi_id,
            date=now.strftime("%Y%m%d"),
            time=now.strftime("%H%M%S"),
            datetime=now.strftime("%Y%m%d_%H%M%S"),
        )
