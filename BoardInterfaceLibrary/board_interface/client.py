from __future__ import annotations

import csv
import socket
import threading
import time
from collections import defaultdict, deque
from dataclasses import asdict
from pathlib import Path
from typing import Optional

from .models import CommandConfig, FileListResponse, PacketBase, StreamResult
from .protocol import (
    CONFIG_READ_HEADER_LEN,
    DAQ_FRAME_LEN,
    FILE_STREAM_HEADER_LEN,
    PACKET_LEN,
    SERVER_ID,
    build_packet,
    parse_fixed_packet,
    parse_stream_header,
    verify_pattern_chunk,
)


class BoardServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 10, backlog: int = 1) -> None:
        self.host = host
        self.port = port
        self.backlog = backlog
        self._socket: Optional[socket.socket] = None

    def listen(self) -> None:
        if self._socket is not None:
            return
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(self.backlog)
        self._socket = server

    def accept(self, timeout: float | None = None) -> "BoardSession":
        if self._socket is None:
            self.listen()
        assert self._socket is not None
        if timeout is not None:
            self._socket.settimeout(timeout)
        conn, addr = self._socket.accept()
        return BoardSession(conn, addr)

    def close(self) -> None:
        if self._socket is not None:
            self._socket.close()
            self._socket = None


class BoardSession:
    def __init__(self, conn: socket.socket, addr: tuple[str, int], server_id: int = SERVER_ID) -> None:
        self.conn = conn
        self.addr = addr
        self.server_id = server_id
        self.default_config = CommandConfig()
        self.last_daq_filename = self.default_config.daq_filename
        self._running = True
        self._send_lock = threading.Lock()
        self._condition = threading.Condition()
        self._responses: dict[int, deque[PacketBase]] = defaultdict(deque)
        self._file_list_result: Optional[FileListResponse] = None
        self._file_list_buffer: Optional[bytearray] = None
        self._stream_result: Optional[StreamResult] = None
        self._stream_event = threading.Event()
        self._stream_state: Optional[dict] = None
        self._thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._running = False
        try:
            self.conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.conn.close()
        self._stream_event.set()
        with self._condition:
            self._condition.notify_all()

    def send_command(self, command: int, *, config: CommandConfig | None = None, epoch_time: int | None = None) -> int:
        if config is None:
            config = self.default_config
        if epoch_time is None:
            epoch_time = int(time.time())
        packet = build_packet(command, self.server_id, epoch_time, config)
        with self._send_lock:
            self.conn.sendall(packet)
        return epoch_time

    def wait_for_command(self, command: int, timeout: float = 5.0) -> PacketBase:
        deadline = time.monotonic() + timeout
        with self._condition:
            while not self._responses[command]:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for command {command}")
                self._condition.wait(remaining)
            return self._responses[command].popleft()

    def heartbeat(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(0)
        return self.wait_for_command(0, timeout)

    def write_config(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(1)
        return self.wait_for_command(1, timeout)

    def get_dat_file_count(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(2)
        return self.wait_for_command(2, timeout)

    def get_all_file_count(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(3)
        return self.wait_for_command(3, timeout)

    def get_file_list(self, timeout: float = 5.0) -> FileListResponse:
        self._file_list_result = None
        self._file_list_buffer = None
        self.send_command(4)
        deadline = time.monotonic() + timeout
        while self._file_list_result is None:
            if time.monotonic() >= deadline:
                raise TimeoutError("Timed out waiting for file list")
            time.sleep(0.01)
        return self._file_list_result

    def delete_log_files(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(6)
        return self.wait_for_command(6, timeout)

    def delete_file(self, filename: str, timeout: float = 5.0) -> PacketBase:
        config = CommandConfig(**asdict(self.default_config))
        config.read_filename = filename
        self.send_command(7, config=config)
        return self.wait_for_command(7, timeout)

    def get_file_size(self, filename: str | None = None, timeout: float = 5.0) -> PacketBase:
        config = CommandConfig(**asdict(self.default_config))
        if filename is not None:
            config.read_filename = filename
        self.send_command(5, config=config)
        return self.wait_for_command(5, timeout)

    def stream_file(self, filename: str | None = None, offset: int = 0, timeout: float = 30.0, verify_pattern: bool = True) -> StreamResult:
        config = CommandConfig(**asdict(self.default_config))
        if filename is not None:
            config.read_filename = filename
        config.stream_offset = offset
        self._prepare_stream(command=8, capture_bytes=True, verify_pattern=verify_pattern, csv_path=None)
        self.send_command(8, config=config)
        return self._wait_for_stream(timeout)

    def test_stream(self, timeout: float = 30.0) -> StreamResult:
        self._prepare_stream(command=9, capture_bytes=True, verify_pattern=False, csv_path=None)
        self.send_command(9)
        return self._wait_for_stream(timeout)

    def get_daq_status(self, log_status: bool = False, timeout: float = 5.0) -> PacketBase:
        command = 110 if log_status else 10
        self.send_command(command)
        return self.wait_for_command(command, timeout)

    def start_daq_log(self, filename: str = "daq.bin", sample_rate_hz: int = 2000, channel_mask: int = 0x3F, block_samples: int = 128, stream_samples: int = 0, timeout: float = 5.0) -> PacketBase:
        config = CommandConfig(**asdict(self.default_config))
        config.daq_filename = filename
        config.daq_sample_rate_hz = sample_rate_hz
        config.daq_channel_mask = channel_mask
        config.daq_block_samples = block_samples
        config.daq_stream_samples = stream_samples
        self.default_config.daq_filename = filename
        self.last_daq_filename = filename
        self.send_command(11, config=config)
        return self.wait_for_command(11, timeout)

    def stop_daq(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(12)
        return self.wait_for_command(12, timeout)

    def stop_daq_log(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(112)
        return self.wait_for_command(112, timeout)

    def start_daq_stream(self, filename: str = "daq.bin", sample_rate_hz: int = 2000, channel_mask: int = 0x3F, block_samples: int = 128, stream_samples: int = 0, csv_path: str | Path | None = None, timeout: float = 30.0) -> StreamResult:
        config = CommandConfig(**asdict(self.default_config))
        config.daq_filename = filename
        config.daq_sample_rate_hz = sample_rate_hz
        config.daq_channel_mask = channel_mask
        config.daq_block_samples = block_samples
        config.daq_stream_samples = stream_samples
        self.default_config.daq_filename = filename
        self.last_daq_filename = filename
        self._prepare_stream(command=13, capture_bytes=False, verify_pattern=False, csv_path=Path(csv_path) if csv_path else None)
        self.send_command(13, config=config)
        return self._wait_for_stream(timeout)

    def openamp_heartbeat(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(99)
        return self.wait_for_command(99, timeout)

    def _prepare_stream(self, *, command: int, capture_bytes: bool, verify_pattern: bool, csv_path: Path | None) -> None:
        self._stream_event.clear()
        self._stream_result = None
        self._stream_state = {
            "command": command,
            "capture_bytes": capture_bytes,
            "verify_pattern": verify_pattern,
            "csv_path": csv_path,
            "server_id": 0,
            "epoch_time": 0,
            "total_size": 0,
            "bytes_received": 0,
            "data": bytearray(),
            "remainder": bytearray(),
            "csv_file": None,
            "csv_writer": None,
            "sample_index": 0,
            "start_monotonic": 0.0,
            "verification_passed": True,
            "verification_error": None,
        }

    def _wait_for_stream(self, timeout: float) -> StreamResult:
        if not self._stream_event.wait(timeout):
            raise TimeoutError("Timed out waiting for stream completion")
        if self._stream_result is None:
            raise RuntimeError("Stream completed without a result")
        return self._stream_result

    def _recv_loop(self) -> None:
        rx_buffer = bytearray()
        while self._running:
            try:
                data = self.conn.recv(16384)
                if not data:
                    break
                rx_buffer.extend(data)
                while rx_buffer:
                    if self._stream_state is not None and self._stream_state["total_size"] > 0:
                        remaining = self._stream_state["total_size"] - self._stream_state["bytes_received"]
                        if remaining <= 0:
                            self._finish_stream()
                            continue
                        consume_len = min(len(rx_buffer), remaining)
                        payload = bytes(rx_buffer[:consume_len])
                        del rx_buffer[:consume_len]
                        self._consume_stream_payload(payload)
                        if self._stream_state is not None and self._stream_state["bytes_received"] >= self._stream_state["total_size"]:
                            self._finish_stream()
                        continue

                    command = rx_buffer[0]
                    if command in {8, 9, 13}:
                        if len(rx_buffer) < FILE_STREAM_HEADER_LEN:
                            break
                        packet = bytes(rx_buffer[:FILE_STREAM_HEADER_LEN])
                        del rx_buffer[:FILE_STREAM_HEADER_LEN]
                        self._handle_stream_header(packet)
                        continue
                    if command == 4:
                        if len(rx_buffer) < CONFIG_READ_HEADER_LEN:
                            break
                        chunk_len = int.from_bytes(rx_buffer[22:24], "big")
                        frame_len = CONFIG_READ_HEADER_LEN + chunk_len
                        if len(rx_buffer) < frame_len:
                            break
                        packet = bytes(rx_buffer[:frame_len])
                        del rx_buffer[:frame_len]
                        self._handle_file_list_packet(packet)
                        continue
                    if len(rx_buffer) < PACKET_LEN:
                        break
                    packet = bytes(rx_buffer[:PACKET_LEN])
                    del rx_buffer[:PACKET_LEN]
                    self._handle_fixed_packet(packet)
            except OSError:
                break
        self._running = False
        self._stream_event.set()
        with self._condition:
            self._condition.notify_all()

    def _handle_fixed_packet(self, packet: bytes) -> None:
        parsed = parse_fixed_packet(packet)
        with self._condition:
            self._responses[parsed.command].append(parsed)
            self._condition.notify_all()

    def _handle_stream_header(self, packet: bytes) -> None:
        command, status, server_id, epoch_time, total_size = parse_stream_header(packet)
        if self._stream_state is None:
            self._prepare_stream(command=command, capture_bytes=(command in {8, 9}), verify_pattern=(command == 8), csv_path=None)
        assert self._stream_state is not None
        self._stream_state["command"] = command
        self._stream_state["server_id"] = server_id
        self._stream_state["epoch_time"] = epoch_time
        self._stream_state["total_size"] = total_size
        self._stream_state["bytes_received"] = 0
        self._stream_state["data"].clear()
        self._stream_state["remainder"].clear()
        self._stream_state["sample_index"] = 0
        self._stream_state["start_monotonic"] = time.monotonic()
        if status != 0 or total_size == 0:
            self._stream_result = StreamResult(command, server_id, epoch_time, total_size, 0)
            self._stream_state = None
            self._stream_event.set()

    def _consume_stream_payload(self, payload: bytes) -> None:
        assert self._stream_state is not None
        state = self._stream_state
        offset = state["bytes_received"]

        if state["verify_pattern"]:
            valid, bad_index, expected, actual = verify_pattern_chunk(offset, payload)
            if not valid:
                state["verification_passed"] = False
                state["verification_error"] = f"offset={offset}, index={bad_index}, expected=0x{expected:02X}, actual=0x{actual:02X}"
                self._finish_stream()
                return

        if state["capture_bytes"]:
            state["data"].extend(payload)
        elif state["command"] == 13 and state["csv_path"] is not None:
            if state["csv_file"] is None:
                state["csv_file"] = state["csv_path"].open("w", newline="")
                state["csv_writer"] = csv.writer(state["csv_file"])
                state["csv_writer"].writerow(["sample_index", "raw_frame_hex"])
            state["remainder"].extend(payload)
            while len(state["remainder"]) >= DAQ_FRAME_LEN:
                frame = bytes(state["remainder"][:DAQ_FRAME_LEN])
                del state["remainder"][:DAQ_FRAME_LEN]
                state["csv_writer"].writerow([state["sample_index"], frame.hex()])
                state["sample_index"] += 1
            state["csv_file"].flush()

        state["bytes_received"] += len(payload)

    def _finish_stream(self) -> None:
        assert self._stream_state is not None
        state = self._stream_state
        if state["csv_file"] is not None:
            state["csv_file"].close()
        elapsed = max(time.monotonic() - state["start_monotonic"], 0.0)
        self._stream_result = StreamResult(
            command=state["command"],
            server_id=state["server_id"],
            epoch_time=state["epoch_time"],
            total_size=state["total_size"],
            bytes_received=state["bytes_received"],
            data=bytes(state["data"]),
            frames_received=state["bytes_received"] // DAQ_FRAME_LEN if state["command"] == 13 else 0,
            csv_path=state["csv_path"],
            verification_passed=state["verification_passed"],
            verification_error=state["verification_error"],
            elapsed_seconds=elapsed,
        )
        self._stream_state = None
        self._stream_event.set()
