from __future__ import annotations

from dataclasses import asdict, is_dataclass
from pathlib import Path
from typing import Any

from .models import (
    CalibrationResponse,
    ConfigWriteResponse,
    DaqAckResponse,
    DaqStatusResponse,
    DeleteResponse,
    FileCountResponse,
    FileListResponse,
    FileSizeResponse,
    OpenAmpHeartbeatResponse,
    PacketBase,
    StreamResult,
    TotalFileCountResponse,
)

EMMC_MOUNT_STAGE_NAMES = {
    0: "NONE",
    1: "LINK",
    2: "MOUNT",
    3: "MKFS",
    4: "POST_MOUNT",
}

FATFS_FRESULT_NAMES = {
    0: "FR_OK",
    1: "FR_DISK_ERR",
    2: "FR_INT_ERR",
    3: "FR_NOT_READY",
    4: "FR_NO_FILE",
    5: "FR_NO_PATH",
    6: "FR_INVALID_NAME",
    7: "FR_DENIED",
    8: "FR_EXIST",
    9: "FR_INVALID_OBJECT",
    10: "FR_WRITE_PROTECTED",
    11: "FR_INVALID_DRIVE",
    12: "FR_NOT_ENABLED",
    13: "FR_NO_FILESYSTEM",
    14: "FR_MKFS_ABORTED",
    15: "FR_TIMEOUT",
    16: "FR_LOCKED",
    17: "FR_NOT_ENOUGH_CORE",
    18: "FR_TOO_MANY_OPEN_FILES",
    19: "FR_INVALID_PARAMETER",
}

DAQ_STREAM_FRAME_BYTES = 36


class ResponseParser:
    """Convert board responses into stable dictionary payloads for end users.

    Keep protocol-specific response shaping here so examples and future user code
    do not need to know the raw dataclass layout.
    """

    @classmethod
    def parse(cls, response: Any) -> Any:
        if response is None:
            return None
        if isinstance(response, FileListResponse):
            return cls._parse_file_list(response)
        if isinstance(response, StreamResult):
            return cls._parse_stream_result(response)
        if isinstance(response, DaqStatusResponse):
            return cls._parse_daq_status(response)
        if isinstance(response, DaqAckResponse):
            return cls._parse_daq_ack(response)
        if isinstance(response, OpenAmpHeartbeatResponse):
            return cls._parse_openamp_heartbeat(response)
        if isinstance(response, FileSizeResponse):
            return cls._parse_dataclass(response)
        if isinstance(response, CalibrationResponse):
            return cls._parse_calibration(response)
        if isinstance(response, (ConfigWriteResponse, FileCountResponse, TotalFileCountResponse, DeleteResponse, PacketBase)):
            return cls._parse_dataclass(response)
        if is_dataclass(response):
            return cls._make_json_safe(asdict(response))
        if isinstance(response, dict):
            return {key: cls.parse(value) for key, value in response.items()}
        if isinstance(response, (list, tuple)):
            return [cls.parse(value) for value in response]
        return response

    @classmethod
    def _parse_dataclass(cls, response: Any) -> dict[str, Any]:
        return cls._make_json_safe(asdict(response))

    @classmethod
    def _parse_file_list(cls, response: FileListResponse) -> dict[str, Any]:
        return {
            "command": response.command,
            "server_id": response.server_id,
            "epoch_time": response.epoch_time,
            "total_size": response.total_size,
            "file_count": len(response.entries),
            "entries": [
                {
                    "name": entry.name,
                    "size": entry.size,
                    "size_mib": round(entry.size / (1024.0 * 1024.0), 6),
                }
                for entry in response.entries
            ],
            "filenames": response.filenames,
        }

    @classmethod
    def _parse_daq_ack(cls, response: DaqAckResponse) -> dict[str, Any]:
        data = cls._parse_dataclass(response)
        data["channel_mask_hex"] = f"0x{response.channel_mask:08X}"
        return data

    @classmethod
    def _parse_daq_status(cls, response: DaqStatusResponse) -> dict[str, Any]:
        data = cls._parse_dataclass(response)
        data["bytes_written_mib"] = round(response.bytes_written / (1024.0 * 1024.0), 6)
        return data

    @classmethod
    def _parse_openamp_heartbeat(cls, response: OpenAmpHeartbeatResponse) -> dict[str, Any]:
        data = cls._parse_dataclass(response)
        data["reply_value_hex"] = f"0x{response.reply_value:08X}"
        data["shmem_probe_bad_index_hex"] = f"0x{response.shmem_probe_bad_index:08X}"
        data["adc_device_id_hex"] = f"0x{response.adc_device_id:04X}"
        data["board_ip_address"] = cls._format_ipv4(response.board_ip)
        data["server_ip_address"] = cls._format_ipv4(response.server_ip)
        data["board_mac_address"] = cls._format_mac(response.board_mac)
        data["emmc_mount_stage_name"] = EMMC_MOUNT_STAGE_NAMES.get(response.emmc_mount_stage, "UNKNOWN")
        data["emmc_mount_fresult_name"] = FATFS_FRESULT_NAMES.get(response.emmc_mount_fresult, "UNKNOWN")
        data["emmc_mkfs_fresult_name"] = FATFS_FRESULT_NAMES.get(response.emmc_mkfs_fresult, "UNKNOWN")
        data["emmc_post_mount_fresult_name"] = FATFS_FRESULT_NAMES.get(response.emmc_post_mount_fresult, "UNKNOWN")
        return data

    @classmethod
    def _parse_calibration(cls, response: CalibrationResponse) -> dict[str, Any]:
        data = cls._parse_dataclass(response)
        data["offsets"] = [
            response.offset_ch0,
            response.offset_ch1,
            response.offset_ch2,
            response.offset_ch3,
            response.offset_ch4,
            response.offset_ch5,
        ]
        return data

    @classmethod
    def _parse_stream_result(cls, response: StreamResult) -> dict[str, Any]:
        elapsed = float(response.elapsed_seconds or 0.0)
        bytes_received = int(response.bytes_received or 0)
        frames_received = int(response.frames_received or 0)
        if response.command == 13 and frames_received == 0 and bytes_received > 0:
            frames_received = bytes_received // DAQ_STREAM_FRAME_BYTES
        return {
            "command": response.command,
            "server_id": response.server_id,
            "epoch_time": response.epoch_time,
            "total_size": response.total_size,
            "bytes_received": bytes_received,
            "bytes_received_mib": round(bytes_received / (1024.0 * 1024.0), 6),
            "frames_received": frames_received,
            "csv_path": str(response.csv_path) if response.csv_path else None,
            "local_path": str(response.local_path) if response.local_path else None,
            "verification_passed": response.verification_passed,
            "verification_error": response.verification_error,
            "elapsed_seconds": elapsed,
            "avg_frames_per_sec": round((frames_received / elapsed), 3) if elapsed > 0.0 else 0.0,
            "avg_mib_per_sec": round(((bytes_received / (1024.0 * 1024.0)) / elapsed), 6) if elapsed > 0.0 else 0.0,
            "data": cls._make_json_safe(response.data),
        }

    @classmethod
    def _make_json_safe(cls, value: Any) -> Any:
        if isinstance(value, bytes):
            return {
                "type": "bytes",
                "length": len(value),
                "hex_preview": value[:32].hex(),
            }
        if isinstance(value, Path):
            return str(value)
        if isinstance(value, dict):
            return {key: cls._make_json_safe(item) for key, item in value.items()}
        if isinstance(value, (list, tuple)):
            return [cls._make_json_safe(item) for item in value]
        return value

    @staticmethod
    def _format_ipv4(value: int) -> str:
        return ".".join(str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0))

    @staticmethod
    def _format_mac(value: bytes) -> str:
        return ":".join(f"{byte:02X}" for byte in value[:6])
