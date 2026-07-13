from __future__ import annotations

import json
import hashlib
import shutil
import threading
import time
import uuid
from pathlib import Path
from typing import Callable

from ..daq import DAQ_STREAM_FRAME_LEN, convert_daq_stream_bin_to_csv
from ..response_parser import ResponseParser
from ..session import BoardSession, StreamHandle
from .database import AemsDatabase


def _safe_board_name(board_ip: str) -> str:
    return board_ip.replace(".", "_").replace(":", "_")


def _metadata_path(metadata_dir: Path, output_path: Path) -> Path:
    return metadata_dir / f"{output_path.name}.metadata.json"


def _build_stream_metrics(result: object, daq_status: object, *, sample_rate_hz: int, channel_mask: int, block_samples: int) -> dict:
    stream = ResponseParser.parse(result) or {}
    status = ResponseParser.parse(daq_status) or {}
    bytes_received = int(stream.get("bytes_received", 0) or 0)
    elapsed_seconds = float(stream.get("elapsed_seconds", 0.0) or 0.0)
    frames_received = int(stream.get("frames_received", 0) or 0) or (bytes_received // DAQ_STREAM_FRAME_LEN)
    samples_captured = int(status.get("samples_captured", 0) or 0)
    dropped_buffers = int(status.get("dropped_buffers", 0) or 0)
    return {
        "requested_sample_rate_hz": sample_rate_hz,
        "channel_mask": channel_mask,
        "channel_mask_hex": f"0x{channel_mask:08X}",
        "block_samples": block_samples,
        "stream_frame_bytes": DAQ_STREAM_FRAME_LEN,
        "bytes_received": bytes_received,
        "bytes_received_mib": round(bytes_received / (1024.0 * 1024.0), 6),
        "elapsed_seconds": round(elapsed_seconds, 3),
        "frames_received": frames_received,
        "stream_frames_per_sec": round(frames_received / elapsed_seconds, 3) if elapsed_seconds > 0 else 0.0,
        "stream_mib_per_sec": round((bytes_received / (1024.0 * 1024.0)) / elapsed_seconds, 6) if elapsed_seconds > 0 else 0.0,
        "board_samples_captured": samples_captured,
        "dropped_buffers": dropped_buffers,
        "estimated_dropped_frames_min": dropped_buffers * block_samples,
        "estimated_unreceived_frames_vs_board_captured": max(samples_captured - frames_received, 0),
        "receive_percent_of_board_captured": round((frames_received / samples_captured) * 100.0, 3) if samples_captured > 0 else 0.0,
    }


class DaqStreamJob:
    """Background host-side capture job for command 13 streams."""

    def __init__(
        self,
        *,
        job_id: str,
        board_ip: str,
        session_getter: Callable[[str], BoardSession],
        database: AemsDatabase,
        output_dir: Path,
        metadata_dir: Path,
        remote_file: str,
        output_format: str,
        duration_s: float | None,
        sample_rate_hz: int,
        channel_mask: int,
        block_samples: int,
        timeout_s: float,
    ) -> None:
        self.job_id = job_id
        self.board_ip = board_ip
        self._session_getter = session_getter
        self._database = database
        self._output_dir = output_dir
        self._metadata_dir = metadata_dir
        self._remote_file = remote_file
        self._output_format = output_format
        self._duration_s = duration_s
        self._sample_rate_hz = sample_rate_hz
        self._channel_mask = channel_mask
        self._block_samples = block_samples
        self._timeout_s = timeout_s
        self._stop_event = threading.Event()
        self._done_event = threading.Event()
        self._thread = threading.Thread(target=self._run, name=f"DaqStreamJob-{board_ip}-{job_id[:8]}", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def request_stop(self) -> None:
        self._stop_event.set()

    def wait(self, timeout: float | None = None) -> bool:
        return self._done_event.wait(timeout)

    @property
    def is_done(self) -> bool:
        return self._done_event.is_set()

    def _run(self) -> None:
        try:
            self._output_dir.mkdir(parents=True, exist_ok=True)
            self._metadata_dir.mkdir(parents=True, exist_ok=True)
            suffix = ".csv" if self._output_format == "csv" else ".bin"
            final_output = self._output_dir / f"{_safe_board_name(self.board_ip)}_{Path(self._remote_file).stem}_{self.job_id[:8]}{suffix}"
            stream_target = final_output if self._output_format == "bin" else final_output.with_suffix(final_output.suffix + ".raw.bin")
            metadata_path = _metadata_path(self._metadata_dir, final_output)
            self._database.update_job(self.job_id, output_path=final_output, metadata_path=metadata_path)

            session = self._session_getter(self.board_ip)
            handle: StreamHandle = session.start_daq_stream_async(
                filename=self._remote_file,
                sample_rate_hz=self._sample_rate_hz,
                channel_mask=self._channel_mask,
                block_samples=self._block_samples,
                csv_path=None,
                local_path=stream_target,
            )

            try:
                start_ack = session.wait_for_command(13, timeout=2.0)
            except TimeoutError:
                # Current firmware may not emit a fixed command-13 ACK before raw stream bytes.
                start_ack = None

            if self._duration_s is None:
                self._stop_event.wait()
            else:
                self._stop_event.wait(self._duration_s)

            self._database.update_job(self.job_id, status="stopping")
            try:
                stop_ack = session.stop_daq(timeout=max(self._timeout_s, 10.0))
            except Exception as exc:
                # Keep waiting for the stream handle so metadata captures partial data.
                stop_ack = {"error": str(exc)}

            result = handle.wait(timeout=max(self._timeout_s, 60.0))
            try:
                daq_status = session.get_daq_status(log_status=False, timeout=max(self._timeout_s, 10.0))
            except Exception as exc:
                daq_status = {"error": str(exc)}

            frames_written = None
            if self._output_format == "csv":
                frames_written = convert_daq_stream_bin_to_csv(stream_target, final_output)
                stream_target.unlink(missing_ok=True)

            metadata = {
                "job_id": self.job_id,
                "board_ip": self.board_ip,
                "remote_file": self._remote_file,
                "output_file": str(final_output),
                "output_format": self._output_format,
                "duration_seconds": self._duration_s,
                "requested_daq_config": {
                    "sample_rate_hz": self._sample_rate_hz,
                    "channel_mask": self._channel_mask,
                    "channel_mask_hex": f"0x{self._channel_mask:08X}",
                    "block_samples": self._block_samples,
                },
                "command_13_ack": ResponseParser.parse(start_ack),
                "command_12_ack": ResponseParser.parse(stop_ack),
                "command_10_status": ResponseParser.parse(daq_status),
                "command_13_stream": ResponseParser.parse(result),
                "frames_written": frames_written,
                "derived_metrics": _build_stream_metrics(
                    result,
                    daq_status,
                    sample_rate_hz=self._sample_rate_hz,
                    channel_mask=self._channel_mask,
                    block_samples=self._block_samples,
                ),
            }
            metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            self._database.update_job(self.job_id, status="complete", result=metadata, stopped=True)
        except Exception as exc:
            self._database.update_job(self.job_id, status="failed", error=str(exc), stopped=True)
        finally:
            self._done_event.set()


class DaqLogJob:
    """Timed command-11 eMMC log job.

    This is different from the live stream job: the board writes to its own eMMC,
    and the host records the ACK/status metadata needed for audit and cloud shadow
    reporting.
    """

    def __init__(
        self,
        *,
        job_id: str,
        board_ip: str,
        session_getter: Callable[[str], BoardSession],
        database: AemsDatabase,
        metadata_dir: Path,
        remote_file: str,
        duration_s: float,
        sample_rate_hz: int,
        channel_mask: int,
        block_samples: int,
        timeout_s: float,
    ) -> None:
        self.job_id = job_id
        self.board_ip = board_ip
        self._session_getter = session_getter
        self._database = database
        self._metadata_dir = metadata_dir
        self._remote_file = remote_file
        self._duration_s = duration_s
        self._sample_rate_hz = sample_rate_hz
        self._channel_mask = channel_mask
        self._block_samples = block_samples
        self._timeout_s = timeout_s
        self._stop_event = threading.Event()
        self._done_event = threading.Event()
        self._thread = threading.Thread(target=self._run, name=f"DaqLogJob-{board_ip}-{job_id[:8]}", daemon=True)

    def start(self) -> None:
        self._thread.start()

    def request_stop(self) -> None:
        self._stop_event.set()

    @property
    def is_done(self) -> bool:
        return self._done_event.is_set()

    def _run(self) -> None:
        try:
            self._metadata_dir.mkdir(parents=True, exist_ok=True)
            metadata_path = self._metadata_dir / f"{_safe_board_name(self.board_ip)}_{Path(self._remote_file).stem}_{self.job_id[:8]}_emmc_log.metadata.json"
            self._database.update_job(self.job_id, metadata_path=metadata_path)
            session = self._session_getter(self.board_ip)
            start_ack = session.start_daq_log(
                filename=self._remote_file,
                sample_rate_hz=self._sample_rate_hz,
                channel_mask=self._channel_mask,
                block_samples=self._block_samples,
                timeout=self._timeout_s,
            )
            self._stop_event.wait(self._duration_s)
            self._database.update_job(self.job_id, status="stopping")
            stop_ack = session.stop_daq_log(timeout=max(self._timeout_s, 10.0))
            try:
                daq_status = session.get_daq_status(log_status=True, timeout=max(self._timeout_s, 10.0))
            except Exception as exc:
                daq_status = {"error": str(exc)}
            metadata = {
                "job_id": self.job_id,
                "board_ip": self.board_ip,
                "remote_file": self._remote_file,
                "duration_seconds": self._duration_s,
                "requested_daq_config": {
                    "sample_rate_hz": self._sample_rate_hz,
                    "channel_mask": self._channel_mask,
                    "channel_mask_hex": f"0x{self._channel_mask:08X}",
                    "block_samples": self._block_samples,
                },
                "command_11_ack": ResponseParser.parse(start_ack),
                "command_112_ack": ResponseParser.parse(stop_ack),
                "command_110_status": ResponseParser.parse(daq_status),
            }
            metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            self._database.update_job(self.job_id, status="complete", result=metadata, stopped=True)
        except Exception as exc:
            self._database.update_job(self.job_id, status="failed", error=str(exc), stopped=True)
        finally:
            self._done_event.set()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class TransferJob:
    """Upload/copy completed host-side capture artifacts and a manifest.

    The manifest is written before transfer starts and transferred last. Cloud
    ingestion can treat manifest arrival as the signal that all listed files are
    available.
    """

    def __init__(
        self,
        *,
        transfer_id: str,
        database: AemsDatabase,
        source_dirs: list[Path],
        manifest_dir: Path,
        site_id: str,
        pi_id: str,
        target: str,
        local_dest: Path | None,
        bucket: str | None,
        prefix: str | None,
        retry_limit: int,
    ) -> None:
        self.transfer_id = transfer_id
        self._database = database
        self._source_dirs = source_dirs
        self._manifest_dir = manifest_dir
        self._site_id = site_id
        self._pi_id = pi_id
        self._target = target
        self._local_dest = local_dest
        self._bucket = bucket
        self._prefix = (prefix or "").strip("/")
        self._retry_limit = max(1, retry_limit)
        self._done_event = threading.Event()
        self._thread = threading.Thread(target=self._run, name=f"TransferJob-{transfer_id[:8]}", daemon=True)

    def start(self) -> None:
        self._thread.start()

    @property
    def is_done(self) -> bool:
        return self._done_event.is_set()

    def _run(self) -> None:
        try:
            self._manifest_dir.mkdir(parents=True, exist_ok=True)
            manifest = self._build_manifest()
            manifest_path = self._manifest_dir / f"{self.transfer_id}.manifest.json"
            manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
            self._database.update_transfer_job(self.transfer_id, manifest_path=str(manifest_path))
            last_error: Exception | None = None
            for attempt in range(1, self._retry_limit + 1):
                try:
                    self._database.update_transfer_job(self.transfer_id, status="running" if attempt == 1 else "retrying", retry_count=attempt - 1)
                    if self._target == "local":
                        result = self._copy_local(manifest, manifest_path)
                    elif self._target == "s3":
                        result = self._upload_s3(manifest, manifest_path)
                    else:
                        raise ValueError(f"Unsupported transfer target: {self._target}")
                    self._database.update_transfer_job(self.transfer_id, status="complete", result=result, completed=True, retry_count=attempt - 1)
                    return
                except Exception as exc:
                    last_error = exc
                    time.sleep(min(2.0 * attempt, 10.0))
            raise RuntimeError(str(last_error) if last_error else "transfer failed")
        except Exception as exc:
            self._database.update_transfer_job(self.transfer_id, status="failed", error=str(exc), completed=True)
        finally:
            self._done_event.set()

    def _build_manifest(self) -> dict:
        files = []
        for source_dir in self._source_dirs:
            if not source_dir.exists():
                continue
            for path in sorted(source_dir.rglob("*")):
                if not path.is_file():
                    continue
                rel = path.relative_to(source_dir.parent)
                files.append({
                    "source_path": str(path),
                    "relative_path": str(rel).replace("\\", "/"),
                    "bytes": path.stat().st_size,
                    "sha256": _sha256_file(path),
                })
        return {
            "manifest_version": 1,
            "transfer_id": self.transfer_id,
            "site_id": self._site_id,
            "pi_id": self._pi_id,
            "created_at": time.time(),
            "target": self._target,
            "s3_bucket": self._bucket,
            "s3_prefix": self._prefix,
            "file_count": len(files),
            "total_bytes": sum(item["bytes"] for item in files),
            "files": files,
        }

    def _copy_local(self, manifest: dict, manifest_path: Path) -> dict:
        if self._local_dest is None:
            raise ValueError("local transfer requires local_dest")
        copied = 0
        bytes_copied = 0
        for item in manifest["files"]:
            src = Path(item["source_path"])
            dst = self._local_dest / item["relative_path"]
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied += 1
            bytes_copied += int(item["bytes"])
        manifest_dst = self._local_dest / "manifests" / manifest_path.name
        manifest_dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(manifest_path, manifest_dst)
        return {"target": "local", "destination": str(self._local_dest), "files_copied": copied, "bytes_copied": bytes_copied, "manifest": str(manifest_dst)}

    def _upload_s3(self, manifest: dict, manifest_path: Path) -> dict:
        if not self._bucket:
            raise ValueError("s3 transfer requires bucket")
        try:
            import boto3  # type: ignore[import-not-found]
        except ImportError as exc:
            raise RuntimeError("boto3 is required for S3 transfer; install .[cloud] into the active environment") from exc
        s3 = boto3.client("s3")
        uploaded = 0
        bytes_uploaded = 0
        for item in manifest["files"]:
            key = "/".join(part for part in (self._prefix, item["relative_path"]) if part)
            s3.upload_file(item["source_path"], self._bucket, key)
            uploaded += 1
            bytes_uploaded += int(item["bytes"])
        manifest_key = "/".join(part for part in (self._prefix, "manifests", manifest_path.name) if part)
        s3.upload_file(str(manifest_path), self._bucket, manifest_key)
        return {"target": "s3", "bucket": self._bucket, "prefix": self._prefix, "files_uploaded": uploaded, "bytes_uploaded": bytes_uploaded, "manifest_key": manifest_key}


class JobManager:
    """Tracks long-running jobs started through aemsctl."""

    def __init__(
        self,
        *,
        database: AemsDatabase,
        session_getter: Callable[[str], BoardSession],
        data_dir: Path,
        capture_dir: Path,
        metadata_dir: Path,
        manifest_dir: Path,
        site_id: str,
        pi_id: str,
        transfer_retry_limit: int,
    ) -> None:
        self._database = database
        self._session_getter = session_getter
        self._data_dir = data_dir
        self._capture_dir = capture_dir
        self._metadata_dir = metadata_dir
        self._manifest_dir = manifest_dir
        self._site_id = site_id
        self._pi_id = pi_id
        self._transfer_retry_limit = transfer_retry_limit
        self._lock = threading.RLock()
        self._jobs: dict[str, DaqStreamJob | DaqLogJob] = {}
        self._transfers: dict[str, TransferJob] = {}

    def start_daq_stream(
        self,
        *,
        board_ip: str,
        remote_file: str,
        output_format: str,
        output_dir: Path | None,
        duration_s: float | None,
        sample_rate_hz: int,
        channel_mask: int,
        block_samples: int,
        timeout_s: float,
    ) -> dict:
        job_id = uuid.uuid4().hex
        target_dir = output_dir or self._capture_dir
        params = {
            "remote_file": remote_file,
            "output_format": output_format,
            "output_dir": str(target_dir),
            "duration_s": duration_s,
            "sample_rate_hz": sample_rate_hz,
            "channel_mask": channel_mask,
            "block_samples": block_samples,
        }
        self._database.create_job(job_id, board_ip, "daq_stream", params)
        job = DaqStreamJob(
            job_id=job_id,
            board_ip=board_ip,
            session_getter=self._session_getter,
            database=self._database,
            output_dir=target_dir,
            metadata_dir=self._metadata_dir,
            remote_file=remote_file,
            output_format=output_format,
            duration_s=duration_s,
            sample_rate_hz=sample_rate_hz,
            channel_mask=channel_mask,
            block_samples=block_samples,
            timeout_s=timeout_s,
        )
        with self._lock:
            self._prune_finished_locked()
            self._jobs[job_id] = job
        job.start()
        return {"job_id": job_id, "board_ip": board_ip, "status": "running", "params": params}

    def start_daq_log_run(
        self,
        *,
        board_ip: str,
        remote_file: str,
        duration_s: float,
        sample_rate_hz: int,
        channel_mask: int,
        block_samples: int,
        timeout_s: float,
    ) -> dict:
        job_id = uuid.uuid4().hex
        params = {
            "remote_file": remote_file,
            "duration_s": duration_s,
            "sample_rate_hz": sample_rate_hz,
            "channel_mask": channel_mask,
            "block_samples": block_samples,
        }
        self._database.create_job(job_id, board_ip, "daq_log_run", params)
        job = DaqLogJob(
            job_id=job_id,
            board_ip=board_ip,
            session_getter=self._session_getter,
            database=self._database,
            metadata_dir=self._metadata_dir,
            remote_file=remote_file,
            duration_s=duration_s,
            sample_rate_hz=sample_rate_hz,
            channel_mask=channel_mask,
            block_samples=block_samples,
            timeout_s=timeout_s,
        )
        with self._lock:
            self._prune_finished_locked()
            self._jobs[job_id] = job
        job.start()
        return {"job_id": job_id, "board_ip": board_ip, "status": "running", "params": params}

    def stop(self, *, job_id: str | None = None, board_ip: str | None = None) -> dict:
        with self._lock:
            self._prune_finished_locked()
            candidates = []
            for current_id, job in self._jobs.items():
                if job.is_done:
                    continue
                if job_id is not None and current_id != job_id:
                    continue
                if board_ip is not None and job.board_ip != board_ip:
                    continue
                candidates.append(job)
        for job in candidates:
            self._database.update_job(job.job_id, status="stopping")
            job.request_stop()
        return {"requested_stop_count": len(candidates), "job_ids": [job.job_id for job in candidates]}

    def list_jobs(self, active: bool | None = None) -> list[dict]:
        return self._database.list_jobs(active=active)

    def active_board_ips(self) -> set[str]:
        with self._lock:
            self._prune_finished_locked()
            return {job.board_ip for job in self._jobs.values() if not job.is_done}

    def start_transfer(self, *, target: str, local_dest: Path | None, bucket: str | None, prefix: str | None) -> dict:
        transfer_id = uuid.uuid4().hex
        source_dirs = [self._capture_dir, self._metadata_dir]
        source_dir_text = ",".join(str(path) for path in source_dirs)
        self._database.create_transfer_job(transfer_id, target=target, source_dir=source_dir_text, bucket=bucket, prefix=prefix)
        job = TransferJob(
            transfer_id=transfer_id,
            database=self._database,
            source_dirs=source_dirs,
            manifest_dir=self._manifest_dir,
            site_id=self._site_id,
            pi_id=self._pi_id,
            target=target,
            local_dest=local_dest,
            bucket=bucket,
            prefix=prefix,
            retry_limit=self._transfer_retry_limit,
        )
        with self._lock:
            self._prune_transfers_locked()
            self._transfers[transfer_id] = job
        job.start()
        return {"transfer_id": transfer_id, "status": "running", "target": target, "source_dirs": source_dir_text}

    def list_transfers(self, active: bool | None = None) -> list[dict]:
        return self._database.list_transfer_jobs(active=active)

    def _prune_finished_locked(self) -> None:
        finished = [job_id for job_id, job in self._jobs.items() if job.is_done]
        for job_id in finished:
            self._jobs.pop(job_id, None)

    def _prune_transfers_locked(self) -> None:
        finished = [transfer_id for transfer_id, job in self._transfers.items() if job.is_done]
        for transfer_id in finished:
            self._transfers.pop(transfer_id, None)
