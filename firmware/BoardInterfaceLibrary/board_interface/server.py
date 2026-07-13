from __future__ import annotations

import socket
import threading
import time
from collections import deque
from typing import Optional

from .models import BoardInfo
from .session import BoardSession


class BoardServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 10, backlog: int = 8) -> None:
        self.host = host
        self.port = port
        self.backlog = backlog
        self._socket: Optional[socket.socket] = None
        self._running = False
        self._accept_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        self._condition = threading.Condition(self._lock)
        self._sessions: dict[str, BoardSession] = {}
        self._new_sessions: deque[BoardSession] = deque()

    def listen(self) -> None:
        if self._socket is not None:
            return
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(self.backlog)
        self._socket = server
        self._running = True
        self._accept_thread = threading.Thread(target=self._accept_loop, name="BoardServerAccept", daemon=True)
        self._accept_thread.start()

    def _accept_loop(self) -> None:
        assert self._socket is not None
        while self._running:
            try:
                conn, addr = self._socket.accept()
            except OSError:
                break

            session = BoardSession(conn, addr)
            with self._condition:
                old_session = self._sessions.get(session.ip_address)
                if old_session is not None:
                    old_session.close()
                self._sessions[session.ip_address] = session
                self._new_sessions.append(session)
                self._condition.notify_all()

    def accept(self, timeout: float | None = None) -> BoardSession:
        self.listen()
        deadline = None if timeout is None else time.monotonic() + timeout
        with self._condition:
            while not self._new_sessions:
                if deadline is None:
                    self._condition.wait()
                    continue
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Timed out waiting for board connection")
                self._condition.wait(remaining)
            return self._new_sessions.popleft()

    def close(self) -> None:
        self._running = False
        if self._socket is not None:
            try:
                self._socket.close()
            except OSError:
                pass
            self._socket = None
        with self._condition:
            sessions = list(self._sessions.values())
            self._sessions.clear()
            self._new_sessions.clear()
            self._condition.notify_all()
        for session in sessions:
            session.close()

    def list_boards(self) -> list[BoardInfo]:
        with self._lock:
            stale = [ip for ip, session in self._sessions.items() if not session.is_online]
            for ip in stale:
                self._sessions.pop(ip, None)
            boards = [session.snapshot() for session in self._sessions.values()]
        boards.sort(key=lambda item: item.ip_address)
        return boards

    def get_board(self, ip_address: str) -> BoardSession:
        with self._lock:
            session = self._sessions.get(ip_address)
            if session is None or not session.is_online:
                raise KeyError(f"Board {ip_address} is not connected")
            return session

    def wait_for_board(self, ip_address: str, timeout: float = 10.0) -> BoardSession:
        deadline = time.monotonic() + timeout
        with self._condition:
            while True:
                session = self._sessions.get(ip_address)
                if session is not None and session.is_online:
                    return session
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for board {ip_address}")
                self._condition.wait(remaining)
