from __future__ import annotations

import json
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any

from ..models import BoardInfo
from ..response_parser import ResponseParser


def _now() -> float:
    return time.time()


def _json(value: Any) -> str | None:
    if value is None:
        return None
    return json.dumps(ResponseParser.parse(value), separators=(",", ":"), sort_keys=True)


def _loads(value: str | None) -> Any:
    if not value:
        return None
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return value


class AemsDatabase:
    """Small SQLite registry for board history and capture jobs.

    The daemon is long-running; SQLite gives shell commands persistent visibility
    into boards that connected in the past, not only boards connected right now.
    """

    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._conn = sqlite3.connect(self.path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self.initialize()

    def close(self) -> None:
        with self._lock:
            self._conn.close()

    def initialize(self) -> None:
        with self._lock:
            self._conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS boards (
                    ip_address TEXT PRIMARY KEY,
                    board_key TEXT NOT NULL,
                    peer_port INTEGER,
                    first_seen REAL NOT NULL,
                    last_seen REAL NOT NULL,
                    last_connected REAL,
                    last_disconnected REAL,
                    active INTEGER NOT NULL DEFAULT 0,
                    connection_count INTEGER NOT NULL DEFAULT 0,
                    latest_heartbeat_json TEXT,
                    latest_openamp_json TEXT,
                    latest_status_json TEXT,
                    last_error TEXT
                );

                CREATE TABLE IF NOT EXISTS board_events (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ip_address TEXT NOT NULL,
                    event_time REAL NOT NULL,
                    event_type TEXT NOT NULL,
                    detail_json TEXT
                );

                CREATE TABLE IF NOT EXISTS jobs (
                    job_id TEXT PRIMARY KEY,
                    board_ip TEXT NOT NULL,
                    kind TEXT NOT NULL,
                    status TEXT NOT NULL,
                    remote_file TEXT,
                    output_path TEXT,
                    metadata_path TEXT,
                    started_at REAL NOT NULL,
                    stopped_at REAL,
                    params_json TEXT,
                    result_json TEXT,
                    error TEXT
                );
                """
            )
            self._conn.commit()

    def mark_board_connected(self, info: BoardInfo) -> None:
        now = _now()
        with self._lock:
            self._conn.execute(
                """
                INSERT INTO boards (
                    ip_address, board_key, peer_port, first_seen, last_seen,
                    last_connected, active, connection_count, last_error
                ) VALUES (?, ?, ?, ?, ?, ?, 1, 1, NULL)
                ON CONFLICT(ip_address) DO UPDATE SET
                    board_key=excluded.board_key,
                    peer_port=excluded.peer_port,
                    last_seen=excluded.last_seen,
                    last_connected=excluded.last_connected,
                    active=1,
                    connection_count=boards.connection_count + 1,
                    last_error=NULL
                """,
                (info.ip_address, info.board_key, info.peer_port, now, now, now),
            )
            self._record_event_locked(info.ip_address, "connect", {"peer_port": info.peer_port})
            self._conn.commit()

    def mark_board_seen(self, info: BoardInfo) -> None:
        with self._lock:
            self._conn.execute(
                """
                UPDATE boards
                SET last_seen=?, peer_port=?, active=?,
                    latest_heartbeat_json=COALESCE(?, latest_heartbeat_json),
                    latest_openamp_json=COALESCE(?, latest_openamp_json)
                WHERE ip_address=?
                """,
                (
                    info.last_seen_at,
                    info.peer_port,
                    1 if info.is_online else 0,
                    _json(info.latest_heartbeat),
                    _json(info.latest_openamp_heartbeat),
                    info.ip_address,
                ),
            )
            self._conn.commit()

    def mark_board_disconnected(self, ip_address: str, reason: str | None = None) -> None:
        now = _now()
        with self._lock:
            self._conn.execute(
                "UPDATE boards SET active=0, last_disconnected=?, last_error=? WHERE ip_address=?",
                (now, reason, ip_address),
            )
            self._record_event_locked(ip_address, "disconnect", {"reason": reason})
            self._conn.commit()

    def update_board_response(self, ip_address: str, response_kind: str, response: Any) -> None:
        column = {
            "heartbeat": "latest_heartbeat_json",
            "openamp": "latest_openamp_json",
            "status": "latest_status_json",
        }.get(response_kind)
        if column is None:
            return
        with self._lock:
            self._conn.execute(
                f"UPDATE boards SET {column}=?, last_seen=? WHERE ip_address=?",
                (_json(response), _now(), ip_address),
            )
            self._conn.commit()

    def list_boards(self, active: bool | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM boards"
        params: tuple[Any, ...] = ()
        if active is not None:
            query += " WHERE active=?"
            params = (1 if active else 0,)
        query += " ORDER BY ip_address"
        with self._lock:
            rows = self._conn.execute(query, params).fetchall()
        return [self._board_row_to_dict(row) for row in rows]

    def create_job(self, job_id: str, board_ip: str, kind: str, params: dict[str, Any]) -> None:
        now = _now()
        with self._lock:
            self._conn.execute(
                """
                INSERT INTO jobs (job_id, board_ip, kind, status, remote_file, output_path, metadata_path, started_at, params_json)
                VALUES (?, ?, ?, 'running', ?, ?, ?, ?, ?)
                """,
                (
                    job_id,
                    board_ip,
                    kind,
                    params.get("remote_file"),
                    str(params.get("output_path")) if params.get("output_path") is not None else None,
                    str(params.get("metadata_path")) if params.get("metadata_path") is not None else None,
                    now,
                    json.dumps(params, default=str, sort_keys=True),
                ),
            )
            self._conn.commit()

    def update_job(
        self,
        job_id: str,
        *,
        status: str | None = None,
        result: Any = None,
        error: str | None = None,
        stopped: bool = False,
        output_path: str | Path | None = None,
        metadata_path: str | Path | None = None,
    ) -> None:
        fields: list[str] = []
        params: list[Any] = []
        if status is not None:
            fields.append("status=?")
            params.append(status)
        if result is not None:
            fields.append("result_json=?")
            params.append(json.dumps(ResponseParser.parse(result), default=str, sort_keys=True))
        if error is not None:
            fields.append("error=?")
            params.append(error)
        if stopped:
            fields.append("stopped_at=?")
            params.append(_now())
        if output_path is not None:
            fields.append("output_path=?")
            params.append(str(output_path))
        if metadata_path is not None:
            fields.append("metadata_path=?")
            params.append(str(metadata_path))
        if not fields:
            return
        params.append(job_id)
        with self._lock:
            self._conn.execute(f"UPDATE jobs SET {', '.join(fields)} WHERE job_id=?", tuple(params))
            self._conn.commit()

    def get_job(self, job_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._conn.execute("SELECT * FROM jobs WHERE job_id=?", (job_id,)).fetchone()
        return None if row is None else self._job_row_to_dict(row)

    def list_jobs(self, active: bool | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM jobs"
        if active is not None:
            query += " WHERE status IN ('running', 'stopping')" if active else " WHERE status NOT IN ('running', 'stopping')"
        query += " ORDER BY started_at DESC"
        with self._lock:
            rows = self._conn.execute(query).fetchall()
        return [self._job_row_to_dict(row) for row in rows]

    def _record_event_locked(self, ip_address: str, event_type: str, detail: dict[str, Any] | None = None) -> None:
        self._conn.execute(
            "INSERT INTO board_events (ip_address, event_time, event_type, detail_json) VALUES (?, ?, ?, ?)",
            (ip_address, _now(), event_type, json.dumps(detail or {}, sort_keys=True)),
        )

    def _board_row_to_dict(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "ip_address": row["ip_address"],
            "board_key": row["board_key"],
            "peer_port": row["peer_port"],
            "first_seen": row["first_seen"],
            "last_seen": row["last_seen"],
            "last_connected": row["last_connected"],
            "last_disconnected": row["last_disconnected"],
            "active": bool(row["active"]),
            "connection_count": row["connection_count"],
            "latest_heartbeat": _loads(row["latest_heartbeat_json"]),
            "latest_openamp": _loads(row["latest_openamp_json"]),
            "latest_status": _loads(row["latest_status_json"]),
            "last_error": row["last_error"],
        }

    def _job_row_to_dict(self, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "job_id": row["job_id"],
            "board_ip": row["board_ip"],
            "kind": row["kind"],
            "status": row["status"],
            "remote_file": row["remote_file"],
            "output_path": row["output_path"],
            "metadata_path": row["metadata_path"],
            "started_at": row["started_at"],
            "stopped_at": row["stopped_at"],
            "params": _loads(row["params_json"]),
            "result": _loads(row["result_json"]),
            "error": row["error"],
        }
