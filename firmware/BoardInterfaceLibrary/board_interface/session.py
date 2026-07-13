from __future__ import annotations

import csv
import socket
import threading
import time
from collections import defaultdict, deque
from dataclasses import replace
from pathlib import Path
from typing import Callable, Optional

from .daq import DAQ_FRAME_LEN, decode_daq_frame
from .models import BoardInfo, CommandConfig, FileListResponse, OpenAmpHeartbeatResponse, PacketBase, StreamResult
from .protocol import (
    CONFIG_READ_HEADER_LEN,
    FILE_STREAM_HEADER_LEN,
    PACKET_LEN,
    SERVER_ID,
    build_packet,
    parse_fixed_packet,
    parse_stream_header,
    verify_pattern_chunk,
)


class StreamHandle:
    def __init__(self, board_ip: str, command: int, local_path: Path | None = None, csv_path: Path | None = None) -> None:
        self.board_ip = board_ip
        self.command = command
        self.local_path = local_path
        self.csv_path = csv_path
        self._event = threading.Event()
        self._lock = threading.Lock()
        self._result: Optional[StreamResult] = None
        self._error: Optional[BaseException] = None
        self._bytes_received = 0
        self._total_size = 0

    def set_progress(self, bytes_received: int, total_size: int) -> None:
        with self._lock:
            self._bytes_received = bytes_received
            self._total_size = total_size

    def set_result(self, result: StreamResult) -> None:
        with self._lock:
            self._result = result
        self._event.set()

    def set_error(self, error: BaseException) -> None:
        with self._lock:
            self._error = error
        self._event.set()

    def progress(self) -> tuple[int, int]:
        with self._lock:
            return self._bytes_received, self._total_size

    def wait(self, timeout: float | None = None) -> StreamResult:
        if not self._event.wait(timeout):
            raise TimeoutError(f"Timed out waiting for stream completion from {self.board_ip}")
        with self._lock:
            if self._error is not None:
                raise RuntimeError(str(self._error)) from self._error
            if self._result is None:
                raise RuntimeError("Stream completed without a result")
            return self._result


class BoardSession:
    def __init__(self, conn: socket.socket, addr: tuple[str, int], server_id: int = SERVER_ID) -> None:
        self.conn = conn
        self.addr = addr
        self.server_id = server_id
        self.ip_address = addr[0]
        self.default_config = CommandConfig()
        self.last_daq_filename = self.default_config.daq_filename
        self.connected_at = time.time()
        self.last_seen_at = self.connected_at
        self.is_online = True
        self.latest_heartbeat: Optional[PacketBase] = None
        self.latest_openamp_heartbeat: Optional[OpenAmpHeartbeatResponse] = None

        self._running = True
        self._send_lock = threading.Lock()
        self._condition = threading.Condition()
        self._responses: dict[int, deque[PacketBase]] = defaultdict(deque)
        self._file_list_result: Optional[FileListResponse] = None
        self._file_list_buffer: Optional[bytearray] = None
        self._active_stream: Optional[dict] = None
        self._thread = threading.Thread(target=self._recv_loop, name=f"BoardSession-{self.ip_address}", daemon=True)
        self._thread.start()

    def snapshot(self) -> BoardInfo:
        return BoardInfo(
            board_key=self.ip_address,
            ip_address=self.ip_address,
            peer_port=self.addr[1],
            connected_at=self.connected_at,
            last_seen_at=self.last_seen_at,
            is_online=self.is_online,
            latest_heartbeat=self.latest_heartbeat,
            latest_openamp_heartbeat=self.latest_openamp_heartbeat,
        )

    def close(self) -> None:
        self._running = False
        self.is_online = False
        try:
            self.conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.conn.close()
        except OSError:
            pass
        if self._active_stream is not None:
            self._active_stream["handle"].set_error(ConnectionError(f"Board {self.ip_address} disconnected"))
            self._active_stream = None
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
                    raise TimeoutError(f"Timed out waiting for command {command} from {self.ip_address}")
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
                raise TimeoutError(f"Timed out waiting for file list from {self.ip_address}")
            time.sleep(0.01)
        return self._file_list_result

    def delete_log_files(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(6)
        return self.wait_for_command(6, timeout)

    def delete_file(self, filename: str, timeout: float = 5.0) -> PacketBase:
        config = replace(self.default_config, read_filename=filename)
        self.send_command(7, config=config)
        return self.wait_for_command(7, timeout)

    def get_file_size(self, filename: str | None = None, timeout: float = 5.0) -> PacketBase:
        config = replace(self.default_config)
        if filename is not None:
            config.read_filename = filename
        self.send_command(5, config=config)
        return self.wait_for_command(5, timeout)

    def stream_file_async(self, filename: str | None = None, offset: int = 0, local_path: str | Path | None = None, verify_pattern: bool = False) -> StreamHandle:
        config = replace(self.default_config)
        if filename is not None:
            config.read_filename = filename
        config.stream_offset = offset
        handle = self._prepare_stream(command=8, capture_bytes=True, verify_pattern=verify_pattern, local_path=Path(local_path) if local_path else None, csv_path=None)
        self.send_command(8, config=config)
        return handle

    def stream_file(self, filename: str | None = None, offset: int = 0, timeout: float = 30.0, verify_pattern: bool = False, local_path: str | Path | None = None) -> StreamResult:
        return self.stream_file_async(filename=filename, offset=offset, local_path=local_path, verify_pattern=verify_pattern).wait(timeout)

    def test_stream_async(self) -> StreamHandle:
        handle = self._prepare_stream(command=9, capture_bytes=True, verify_pattern=False, local_path=None, csv_path=None)
        self.send_command(9)
        return handle

    def test_stream(self, timeout: float = 30.0) -> StreamResult:
        return self.test_stream_async().wait(timeout)

    def get_daq_status(self, log_status: bool = False, timeout: float = 5.0) -> PacketBase:
        command = 110 if log_status else 10
        self.send_command(command)
        return self.wait_for_command(command, timeout)

    def get_offset_calibration(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(97)
        return self.wait_for_command(97, timeout)

    def run_offset_calibration(self, timeout: float = 25.0) -> PacketBase:
        self.send_command(98)
        return self.wait_for_command(98, timeout)

    def start_daq_log(self, filename: str = "daq.bin", sample_rate_hz: int = 2000, channel_mask: int = 0x3F, block_samples: int = 128, stream_samples: int = 0, timeout: float = 5.0) -> PacketBase:
        config = replace(
            self.default_config,
            daq_filename=filename,
            daq_sample_rate_hz=sample_rate_hz,
            daq_channel_mask=channel_mask,
            daq_block_samples=block_samples,
            daq_stream_samples=stream_samples,
        )
        self.default_config = replace(config)
        self.last_daq_filename = filename
        self.send_command(11, config=config)
        return self.wait_for_command(11, timeout)

    def stop_daq(self, timeout: float = 5.0) -> PacketBase:
        if self._active_stream is not None and self._active_stream["command"] == 13:
            self._active_stream["control_commands_pending"].add(12)
        self.send_command(12)
        return self.wait_for_command(12, timeout)

    def stop_daq_log(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(112)
        return self.wait_for_command(112, timeout)

    def start_daq_stream_async(
        self,
        filename: str = "daq.bin",
        sample_rate_hz: int = 2000,
        channel_mask: int = 0x3F,
        block_samples: int = 128,
        stream_samples: int = 0,
        csv_path: str | Path | None = None,
        local_path: str | Path | None = None,
        sample_callback: Callable[[int, list[float]], None] | None = None,
    ) -> StreamHandle:
        config = replace(
            self.default_config,
            daq_filename=filename,
            daq_sample_rate_hz=sample_rate_hz,
            daq_channel_mask=channel_mask,
            daq_block_samples=block_samples,
            daq_stream_samples=stream_samples,
        )
        self.default_config = replace(config)
        self.last_daq_filename = filename
        handle = self._prepare_stream(
            command=13,
            capture_bytes=False,
            verify_pattern=False,
            local_path=Path(local_path) if local_path else None,
            csv_path=Path(csv_path) if csv_path else (None if sample_callback is not None else (self._default_stream_csv_path(filename) if local_path is None else None)),
            sample_callback=sample_callback,
        )
        self.send_command(13, config=config)
        return handle

    def start_daq_stream(self, filename: str = "daq.bin", sample_rate_hz: int = 2000, channel_mask: int = 0x3F, block_samples: int = 128, stream_samples: int = 0, csv_path: str | Path | None = None, local_path: str | Path | None = None, timeout: float = 30.0) -> StreamResult:
        return self.start_daq_stream_async(
            filename=filename,
            sample_rate_hz=sample_rate_hz,
            channel_mask=channel_mask,
            block_samples=block_samples,
            stream_samples=stream_samples,
            csv_path=csv_path,
            local_path=local_path,
        ).wait(timeout)

    def openamp_heartbeat(self, timeout: float = 5.0) -> PacketBase:
        self.send_command(99)
        return self.wait_for_command(99, timeout)

    def _prepare_stream(
        self,
        *,
        command: int,
        capture_bytes: bool,
        verify_pattern: bool,
        local_path: Path | None,
        csv_path: Path | None,
        sample_callback: Callable[[int, list[float]], None] | None = None,
    ) -> StreamHandle:
        if self._active_stream is not None:
            raise RuntimeError(f"Board {self.ip_address} already has an active stream")

        handle = StreamHandle(self.ip_address, command, local_path=local_path, csv_path=csv_path)
        self._active_stream = {
            "handle": handle,
            "command": command,
            "capture_bytes": capture_bytes,
            "verify_pattern": verify_pattern,
            "local_path": local_path,
            "csv_path": csv_path,
            "sample_callback": sample_callback,
            "server_id": 0,
            "epoch_time": 0,
            "total_size": 0,
            "bytes_received": 0,
            "data": bytearray(),
            "remainder": bytearray(),
            "csv_file": None,
            "csv_writer": None,
            "binary_file": None,
            "sample_index": 0,
            "start_monotonic": 0.0,
            "verification_passed": True,
            "verification_error": None,
            "control_commands_pending": set(),
        }
        return handle

    def _default_stream_csv_path(self, filename: str) -> Path:
        stem = Path(filename).stem or "stream"
        captures_dir = Path("captures")
        captures_dir.mkdir(parents=True, exist_ok=True)
        return captures_dir / f"{self.ip_address}_{stem}.csv"

    def _find_stream_control_marker(self, buffer: bytearray) -> int:
        if self._active_stream is None:
            return -1

        pending_commands = self._active_stream["control_commands_pending"]
        if not pending_commands:
            return -1

        now = int(time.time())
        limit = max(0, len(buffer) - PACKET_LEN + 1)
        for index in range(limit):
            command = buffer[index]
            if command not in pending_commands:
                continue
            server_id = int.from_bytes(buffer[index + 1:index + 5], "big")
            if server_id != self.server_id:
                continue
            epoch_time = int.from_bytes(buffer[index + 5:index + 13], "big")
            if epoch_time > (now + 3600):
                continue
            return index
        return -1

    def _recv_loop(self) -> None:
        rx_buffer = bytearray()
        while self._running:
            try:
                data = self.conn.recv(16384)
                if not data:
                    break
                rx_buffer.extend(data)
                while rx_buffer:
                    if self._active_stream is not None and self._active_stream["total_size"] > 0:
                        if self._active_stream["command"] == 13:
                            marker_index = self._find_stream_control_marker(rx_buffer)
                            if marker_index >= 0:
                                if marker_index > 0:
                                    payload = bytes(rx_buffer[:marker_index])
                                    del rx_buffer[:marker_index]
                                    self._consume_stream_payload(payload)
                                command = rx_buffer[0]
                                if command == 12:
                                    self._finish_stream()
                                packet = bytes(rx_buffer[:PACKET_LEN])
                                del rx_buffer[:PACKET_LEN]
                                self._handle_fixed_packet(packet)
                                if self._active_stream is not None:
                                    self._active_stream["control_commands_pending"].discard(command)
                                continue
                            if self._active_stream["control_commands_pending"] and len(rx_buffer) < PACKET_LEN:
                                break
                            if self._active_stream["control_commands_pending"]:
                                consume_len = len(rx_buffer) - (PACKET_LEN - 1)
                                if consume_len > 0:
                                    payload = bytes(rx_buffer[:consume_len])
                                    del rx_buffer[:consume_len]
                                    self._consume_stream_payload(payload)
                                    continue

                        remaining = self._active_stream["total_size"] - self._active_stream["bytes_received"]
                        if remaining <= 0:
                            self._finish_stream()
                            continue
                        consume_len = min(len(rx_buffer), remaining)
                        payload = bytes(rx_buffer[:consume_len])
                        del rx_buffer[:consume_len]
                        self._consume_stream_payload(payload)
                        if self._active_stream is not None and self._active_stream["bytes_received"] >= self._active_stream["total_size"]:
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
        self.is_online = False
        if self._active_stream is not None:
            self._active_stream["handle"].set_error(ConnectionError(f"Board {self.ip_address} disconnected"))
            self._active_stream = None
        with self._condition:
            self._condition.notify_all()

    def _handle_fixed_packet(self, packet: bytes) -> None:
        parsed = parse_fixed_packet(packet)
        self.last_seen_at = time.time()
        if parsed.command == 0:
            self.latest_heartbeat = parsed
        elif parsed.command == 99 and isinstance(parsed, OpenAmpHeartbeatResponse):
            self.latest_openamp_heartbeat = parsed
        with self._condition:
            self._responses[parsed.command].append(parsed)
            self._condition.notify_all()

    def _handle_file_list_packet(self, packet: bytes) -> None:
        command = packet[0]
        server_id = int.from_bytes(packet[1:5], "big")
        epoch_time = int.from_bytes(packet[5:13], "big")
        total_size = int.from_bytes(packet[14:18], "big")
        offset = int.from_bytes(packet[18:22], "big")
        chunk_len = int.from_bytes(packet[22:24], "big")
        chunk = packet[24:24 + chunk_len]
        self.last_seen_at = time.time()

        if total_size == 0:
            self._file_list_result = FileListResponse(command=command, server_id=server_id, epoch_time=epoch_time, total_size=0, data=b"")
            self._file_list_buffer = bytearray()
            return

        if self._file_list_buffer is None or len(self._file_list_buffer) != total_size:
            self._file_list_buffer = bytearray(total_size)

        if chunk_len > 0 and (offset + chunk_len) <= len(self._file_list_buffer):
            self._file_list_buffer[offset:offset + chunk_len] = chunk

        if (offset + chunk_len) >= total_size:
            self._file_list_result = FileListResponse(
                command=command,
                server_id=server_id,
                epoch_time=epoch_time,
                total_size=total_size,
                data=bytes(self._file_list_buffer),
            )

    def _handle_stream_header(self, packet: bytes) -> None:
        command, status, server_id, epoch_time, total_size = parse_stream_header(packet)
        if self._active_stream is None:
            handle = StreamHandle(self.ip_address, command)
            handle.set_error(RuntimeError(f"Unexpected stream header from {self.ip_address}"))
            return

        state = self._active_stream
        if state["command"] != command:
            state["handle"].set_error(RuntimeError(f"Unexpected stream command {command} from {self.ip_address}"))
            self._active_stream = None
            return

        self.last_seen_at = time.time()
        state["server_id"] = server_id
        state["epoch_time"] = epoch_time
        state["total_size"] = total_size
        state["bytes_received"] = 0
        state["data"].clear()
        state["remainder"].clear()
        state["sample_index"] = 0
        state["start_monotonic"] = time.monotonic()
        state["handle"].set_progress(0, total_size)
        if status != 0 or total_size == 0:
            self._finish_stream(error=RuntimeError(f"Stream start failed from {self.ip_address}: status={status}, total_size={total_size}"))
            return

        if state["csv_path"] is not None:
            state["csv_path"].parent.mkdir(parents=True, exist_ok=True)
        if state["local_path"] is not None:
            state["local_path"].parent.mkdir(parents=True, exist_ok=True)

    def _consume_stream_payload(self, payload: bytes) -> None:
        assert self._active_stream is not None
        state = self._active_stream
        offset = state["bytes_received"]

        if state["verify_pattern"]:
            valid, bad_index, expected, actual = verify_pattern_chunk(offset, payload)
            if not valid:
                state["verification_passed"] = False
                state["verification_error"] = f"offset={offset}, index={bad_index}, expected=0x{expected:02X}, actual=0x{actual:02X}"
                self._finish_stream(error=RuntimeError(state["verification_error"]))
                return

        if state["capture_bytes"]:
            state["data"].extend(payload)
            if state["local_path"] is not None:
                if state["binary_file"] is None:
                    state["binary_file"] = state["local_path"].open("wb")
                state["binary_file"].write(payload)
                state["binary_file"].flush()
        elif state["command"] == 13:
            if state["local_path"] is not None:
                if state["binary_file"] is None:
                    state["binary_file"] = state["local_path"].open("wb")
                state["binary_file"].write(payload)
                state["binary_file"].flush()
            if (state["csv_path"] is not None) or (state["sample_callback"] is not None):
                if state["csv_file"] is None:
                    if state["csv_path"] is not None:
                        state["csv_file"] = state["csv_path"].open("w", newline="")
                        state["csv_writer"] = csv.writer(state["csv_file"])
                        state["csv_writer"].writerow(["sample_index", "ch0", "ch1", "ch2", "ch3", "ch4", "ch5"])
                state["remainder"].extend(payload)
                while len(state["remainder"]) >= DAQ_FRAME_LEN:
                    frame = bytes(state["remainder"][:DAQ_FRAME_LEN])
                    del state["remainder"][:DAQ_FRAME_LEN]
                    values = decode_daq_frame(frame)
                    if state["csv_writer"] is not None:
                        state["csv_writer"].writerow([state["sample_index"], *values])
                    if state["sample_callback"] is not None:
                        state["sample_callback"](state["sample_index"], values)
                    state["sample_index"] += 1
                if state["csv_file"] is not None:
                    state["csv_file"].flush()

        state["bytes_received"] += len(payload)
        state["handle"].set_progress(state["bytes_received"], state["total_size"])
        self.last_seen_at = time.time()

    def _finish_stream(self, error: BaseException | None = None) -> None:
        assert self._active_stream is not None
        state = self._active_stream
        if state["csv_file"] is not None:
            state["csv_file"].close()
        if state["binary_file"] is not None:
            state["binary_file"].close()
        elapsed = max(time.monotonic() - state["start_monotonic"], 0.0)
        result = StreamResult(
            command=state["command"],
            server_id=state["server_id"],
            epoch_time=state["epoch_time"],
            total_size=state["total_size"],
            bytes_received=state["bytes_received"],
            data=bytes(state["data"]),
            frames_received=state["sample_index"],
            csv_path=state["csv_path"],
            local_path=state["local_path"],
            verification_passed=state["verification_passed"],
            verification_error=state["verification_error"],
            elapsed_seconds=elapsed,
        )
        handle = state["handle"]
        self._active_stream = None
        if error is not None:
            handle.set_error(error)
        else:
            handle.set_result(result)
