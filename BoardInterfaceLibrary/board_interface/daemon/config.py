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
    database_path: Path = Path("/var/lib/aems-server/aems.db")
    api_socket: Path = Path("/run/aems-server/aems-boardd.sock")
    accept_timeout_s: float = 1.0
    poll_interval_s: float = 2.0

    @classmethod
    def from_mapping(cls, data: dict[str, Any]) -> "DaemonConfig":
        default = cls()
        data_dir = Path(str(data.get("data_dir", default.data_dir))).expanduser()
        capture_dir = Path(str(data.get("capture_dir", data_dir / "captures"))).expanduser()
        metadata_dir = Path(str(data.get("metadata_dir", data_dir / "metadata"))).expanduser()
        database_path = Path(str(data.get("database_path", data_dir / "aems.db"))).expanduser()
        return cls(
            host=str(data.get("host", default.host)),
            port=int(data.get("port", default.port)),
            backlog=int(data.get("backlog", default.backlog)),
            data_dir=data_dir,
            capture_dir=capture_dir,
            metadata_dir=metadata_dir,
            database_path=database_path,
            api_socket=Path(str(data.get("api_socket", default.api_socket))).expanduser(),
            accept_timeout_s=float(data.get("accept_timeout_s", default.accept_timeout_s)),
            poll_interval_s=float(data.get("poll_interval_s", default.poll_interval_s)),
        )

    def ensure_directories(self) -> None:
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self.capture_dir.mkdir(parents=True, exist_ok=True)
        self.metadata_dir.mkdir(parents=True, exist_ok=True)
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
