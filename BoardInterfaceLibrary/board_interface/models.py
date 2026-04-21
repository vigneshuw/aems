from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(slots=True)
class CommandConfig:
    read_filename: str = "test.dat"
    daq_filename: str = "daq.bin"
    daq_sample_rate_hz: int = 2000
    daq_channel_mask: int = 0x3F
    daq_block_samples: int = 128
    daq_stream_samples: int = 0
    chunk_offset: int = 0
    stream_offset: int = 0


@dataclass(slots=True)
class PacketBase:
    command: int
    server_id: int
    epoch_time: int
    status: int
    tcp_connected: int


@dataclass(slots=True)
class ConfigWriteResponse(PacketBase):
    fs_status: int


@dataclass(slots=True)
class FileCountResponse(PacketBase):
    dat_count: int
    fs_status: int


@dataclass(slots=True)
class TotalFileCountResponse(PacketBase):
    total_count: int
    fs_status: int


@dataclass(slots=True)
class FileSizeResponse(PacketBase):
    file_size: int
    fs_status: int




@dataclass(slots=True)
class OpenAmpHeartbeatResponse(PacketBase):
    reply_value: int
    fs_status: int
    service_created: int
    rx_count: int
    init_status: int
    remote_init_status: int
    remote_mount_status: int
    shmem_probe_status: int
    shmem_probe_len: int
    shmem_probe_bad_index: int
    stream_open_status: int
    stream_prefetch_status: int
    stream_prefetch_len: int
    adc_device_id: int


@dataclass(slots=True)
class DaqStatusResponse(PacketBase):
    op_status: int
    state: int
    mode: int
    last_error: int
    samples_captured: int
    dropped_buffers: int
    bytes_written: int
    adc_ready_pending: int


@dataclass(slots=True)
class DaqAckResponse(PacketBase):
    op_status: int
    sample_rate_hz: int
    channel_mask: int
    block_samples: int






@dataclass(slots=True)
class StreamResult:
    command: int
    server_id: int
    epoch_time: int
    total_size: int
    bytes_received: int
    data: bytes = b""
    frames_received: int = 0
    csv_path: Optional[Path] = None
    verification_passed: bool = True
    verification_error: Optional[str] = None
    elapsed_seconds: float = 0.0


@dataclass(slots=True)
class FileListEntry:
    name: str
    size: int


@dataclass(slots=True)
class FileListResponse:
    command: int
    server_id: int
    epoch_time: int
    total_size: int
    data: bytes

    @property
    def entries(self) -> list[FileListEntry]:
        if not self.data:
            return []

        result: list[FileListEntry] = []
        for line in self.data.decode("utf-8", errors="replace").splitlines():
            if not line:
                continue
            if "	" in line:
                name, size_text = line.split("	", 1)
                try:
                    size = int(size_text.strip())
                except ValueError:
                    size = 0
                result.append(FileListEntry(name=name, size=size))
            else:
                result.append(FileListEntry(name=line, size=0))
        return result

    @property
    def filenames(self) -> list[str]:
        return [entry.name for entry in self.entries]


@dataclass(slots=True)
class DeleteResponse(PacketBase):
    op_status: int
    deleted_count: int
