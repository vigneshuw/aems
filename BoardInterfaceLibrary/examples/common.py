from __future__ import annotations

import argparse
import time
from pathlib import Path

from board_interface import BoardServer, BoardSession


def build_parser(description: str) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--host", default="0.0.0.0", help="Host interface to listen on")
    parser.add_argument("--port", type=int, default=10, help="TCP port to listen on")
    parser.add_argument("--board-ip", help="Target board IP. If omitted, wait for the next board connection.")
    parser.add_argument("--timeout", type=float, default=30.0, help="Seconds to wait for a board connection")
    return parser


def open_server(host: str, port: int) -> BoardServer:
    # The library runs as the TCP server; boards initiate the connection to this host.
    server = BoardServer(host=host, port=port)
    server.listen()
    print(f"Listening on {host}:{port}")
    return server


def wait_for_session(server: BoardServer, board_ip: str | None, timeout: float) -> BoardSession:
    # Single-board examples either wait for a specific IP or accept the next board that connects.
    if board_ip:
        print(f"Waiting for board {board_ip}...")
        session = server.wait_for_board(board_ip, timeout=timeout)
    else:
        print("Waiting for next board connection...")
        session = server.accept(timeout=timeout)
    print(f"Using board {session.ip_address}:{session.addr[1]}")
    return session


def ensure_directory(path: Path) -> Path:
    # Central helper so every example can safely write outputs without repeating boilerplate.
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def sleep_with_progress(seconds: float) -> None:
    # A small progress display makes DAQ examples easier to follow during long waits.
    end = time.monotonic() + seconds
    while True:
        remaining = end - time.monotonic()
        if remaining <= 0:
            break
        print(f"  remaining: {remaining:5.1f}s", end="\r", flush=True)
        time.sleep(min(0.5, remaining))
    print(" " * 32, end="\r", flush=True)
