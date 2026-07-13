from __future__ import annotations

import json
import os
import socket
import threading
from pathlib import Path
from typing import Any, Callable


class LocalApiServer:
    """JSON-lines Unix socket API used by aemsctl.

    The daemon owns TCP port 10. The CLI never listens for boards; it sends local
    commands to this socket so one Raspberry Pi service can coordinate all boards.
    """

    def __init__(self, socket_path: str | Path, handler: Callable[[dict[str, Any]], dict[str, Any]]) -> None:
        self.socket_path = Path(socket_path)
        self._handler = handler
        self._socket: socket.socket | None = None
        self._running = False
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if not hasattr(socket, "AF_UNIX"):
            raise RuntimeError("Unix domain sockets are required for aems-boardd")
        self.socket_path.parent.mkdir(parents=True, exist_ok=True)
        if self.socket_path.exists():
            self.socket_path.unlink()
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(self.socket_path))
        os.chmod(self.socket_path, 0o660)
        server.listen(16)
        self._socket = server
        self._running = True
        self._thread = threading.Thread(target=self._accept_loop, name="AemsLocalApi", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._running = False
        if self._socket is not None:
            try:
                self._socket.close()
            except OSError:
                pass
            self._socket = None
        try:
            self.socket_path.unlink()
        except FileNotFoundError:
            pass

    def _accept_loop(self) -> None:
        assert self._socket is not None
        while self._running:
            try:
                conn, _ = self._socket.accept()
            except OSError:
                break
            threading.Thread(target=self._handle_client, args=(conn,), name="AemsLocalApiClient", daemon=True).start()

    def _handle_client(self, conn: socket.socket) -> None:
        with conn:
            reader = conn.makefile("r", encoding="utf-8")
            writer = conn.makefile("w", encoding="utf-8")
            for line in reader:
                try:
                    request = json.loads(line)
                    response = self._handler(request)
                except Exception as exc:
                    response = {"ok": False, "error": str(exc)}
                writer.write(json.dumps(response, default=str) + "\n")
                writer.flush()
