from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:  # Python 3.11+
    import tomllib  # type: ignore[attr-defined]
except ModuleNotFoundError:  # pragma: no cover - only used on Python 3.10
    tomllib = None  # type: ignore[assignment]


@dataclass(slots=True)
class DaemonConfig:
    """Runtime paths and network settings for the Raspberry Pi daemon."""

    host: str = "0.0.0.0"
    port: int = 10
    backlog: int = 16
    data_dir: Path = Path("/var/lib/aems-server")
    capture_dir: Path = Path("/var/lib/aems-server/captures")
    metadata_dir: Path = Path("/var/lib/aems-server/metadata")
    manifest_dir: Path = Path("/var/lib/aems-server/manifests")
    database_path: Path = Path("/var/lib/aems-server/aems.db")
    api_socket: Path = Path("/run/aems-server/aems-boardd.sock")
    site_id: str = "site-001"
    pi_id: str = "pi-001"
    accept_timeout_s: float = 1.0
    poll_interval_s: float = 2.0
    health_poll_interval_s: float = 30.0
    schedule_poll_interval_s: float = 15.0
    transfer_retry_limit: int = 3
    aws_region: str = ""
    aws_iot_endpoint: str = ""
    aws_s3_bucket: str = ""
    aws_s3_prefix: str = "aems/"

    @classmethod
    def from_mapping(cls, data: dict[str, Any]) -> "DaemonConfig":
        default = cls()
        data_dir = Path(str(data.get("data_dir", default.data_dir))).expanduser()
        capture_dir = Path(str(data.get("capture_dir", data_dir / "captures"))).expanduser()
        metadata_dir = Path(str(data.get("metadata_dir", data_dir / "metadata"))).expanduser()
        manifest_dir = Path(str(data.get("manifest_dir", data_dir / "manifests"))).expanduser()
        database_path = Path(str(data.get("database_path", data_dir / "aems.db"))).expanduser()
        return cls(
            host=str(data.get("host", default.host)),
            port=int(data.get("port", default.port)),
            backlog=int(data.get("backlog", default.backlog)),
            data_dir=data_dir,
            capture_dir=capture_dir,
            metadata_dir=metadata_dir,
            manifest_dir=manifest_dir,
            database_path=database_path,
            api_socket=Path(str(data.get("api_socket", default.api_socket))).expanduser(),
            site_id=str(data.get("site_id", default.site_id)),
            pi_id=str(data.get("pi_id", default.pi_id)),
            accept_timeout_s=float(data.get("accept_timeout_s", default.accept_timeout_s)),
            poll_interval_s=float(data.get("poll_interval_s", default.poll_interval_s)),
            health_poll_interval_s=float(data.get("health_poll_interval_s", default.health_poll_interval_s)),
            schedule_poll_interval_s=float(data.get("schedule_poll_interval_s", default.schedule_poll_interval_s)),
            transfer_retry_limit=int(data.get("transfer_retry_limit", default.transfer_retry_limit)),
            aws_region=str(data.get("aws_region", default.aws_region)),
            aws_iot_endpoint=str(data.get("aws_iot_endpoint", default.aws_iot_endpoint)),
            aws_s3_bucket=str(data.get("aws_s3_bucket", default.aws_s3_bucket)),
            aws_s3_prefix=str(data.get("aws_s3_prefix", default.aws_s3_prefix)),
        )

    def ensure_directories(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.capture_dir.mkdir(parents=True, exist_ok=True)
        self.metadata_dir.mkdir(parents=True, exist_ok=True)
        self.manifest_dir.mkdir(parents=True, exist_ok=True)
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        self.api_socket.parent.mkdir(parents=True, exist_ok=True)


def load_config(path: str | Path | None) -> DaemonConfig:
    """Load TOML config when present, otherwise return safe defaults."""

    if path is None:
        return DaemonConfig()
    config_path = Path(path).expanduser()
    if not config_path.exists():
        return DaemonConfig()
    if tomllib is None:
        raise RuntimeError("Python 3.10 requires the optional tomli package to read TOML config files")
    with config_path.open("rb") as handle:
        data = tomllib.load(handle)
    return DaemonConfig.from_mapping(data)
