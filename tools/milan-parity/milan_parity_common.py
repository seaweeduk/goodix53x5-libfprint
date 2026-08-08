"""Shared strict I/O and contract helpers for Milan dump replay."""

from __future__ import annotations

import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
from typing import Any, Iterator


RUNTIME_SCHEMA = "goodix53x5-runtime-debug/v3"
DRIVER_BUILD_SCHEMA = "milan-parity-driver-build/v2"
CASE_SCHEMA = "milan-parity-case/v1"
CORPUS_SCHEMA = "milan-parity-corpus/v1"
RECORD_SCHEMA = "milan-parity-record/v1"
REPORT_SCHEMA = "milan-parity-dump-report/v2"
POLICY = {
    "anti_fake_mode": 1,
    "boundary_policy": "canonical-zero-v1",
    "print_schema": 4,
    "profile": 9,
    "subtype": 12,
}
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1


class HarnessError(RuntimeError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise HarnessError(f"cannot read {path}: {error.strerror or error}") from error
    return digest.hexdigest()


def load_json(path: Path, label: str = "JSON") -> Any:
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise HarnessError(f"cannot read {label} {path}: {error}") from error
    if raw != canonical(value):
        raise HarnessError(f"{label} is not canonical JSON: {path}")
    return value


def write_atomic(path: Path, value: bytes, mode: int = 0o600) -> None:
    try:
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        partial = path.with_name(f".{path.name}.partial-{os.getpid()}")
        fd = os.open(partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(value)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(partial, path)
        finally:
            with contextlib.suppress(FileNotFoundError):
                partial.unlink()
    except OSError as error:
        raise HarnessError(f"cannot write {path}: {error.strerror or error}") from error


def write_new_atomic(path: Path, value: bytes, mode: int = 0o600) -> None:
    try:
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        partial = path.with_name(f".{path.name}.partial-{os.getpid()}")
        fd = os.open(partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(value)
                stream.flush()
                os.fsync(stream.fileno())
            os.link(partial, path)
        finally:
            with contextlib.suppress(FileNotFoundError):
                partial.unlink()
    except FileExistsError as error:
        raise HarnessError(f"refusing to overwrite existing file: {path}") from error
    except OSError as error:
        raise HarnessError(f"cannot write {path}: {error.strerror or error}") from error


def ensure_private_directory(path: Path, create: bool = False) -> Path:
    path = path.expanduser().resolve()
    try:
        if create:
            path.mkdir(mode=0o700, parents=True, exist_ok=True)
        metadata = path.stat()
    except OSError as error:
        raise HarnessError(f"private directory is unavailable: {path}: {error.strerror or error}") from error
    if not stat.S_ISDIR(metadata.st_mode):
        raise HarnessError(f"not a directory: {path}")
    if metadata.st_mode & 0o077:
        raise HarnessError(f"private directory must be owner-only (0700): {path}")
    if metadata.st_uid != os.getuid():
        raise HarnessError(f"private directory is not owned by the invoking user: {path}")
    return path


def contained(root: Path, relative: str, label: str) -> Path:
    if not isinstance(relative, str):
        raise HarnessError(f"{label} path must be a string")
    member = Path(relative)
    if member.is_absolute() or not member.parts or ".." in member.parts:
        raise HarnessError(f"{label} path must be relative and contained")
    current = root
    for part in member.parts:
        current = current / part
        if current.is_symlink():
            raise HarnessError(f"{label} must not traverse a symlink: {relative}")
    resolved = (root / member).resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise HarnessError(f"{label} escapes the dump: {relative}") from error
    return resolved


def require_exact_keys(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise HarnessError(f"{label} must be an object")
    missing = sorted(keys - value.keys())
    unexpected = sorted(value.keys() - keys)
    if missing or unexpected:
        detail = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if unexpected:
            detail.append("unexpected " + ", ".join(unexpected))
        raise HarnessError(f"{label} fields differ: {'; '.join(detail)}")
    return value


def require_uint(value: Any, maximum: int, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum:
        raise HarnessError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def require_int32(value: Any, label: str) -> int:
    if (not isinstance(value, int) or isinstance(value, bool) or
            not -(1 << 31) <= value < (1 << 31)):
        raise HarnessError(f"{label} must be an int32")
    return value


def require_sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise HarnessError(f"{label} must be a lowercase SHA-256")
    return value


def run(command: tuple[str, ...] | list[str], label: str, **kwargs: Any) -> subprocess.CompletedProcess:
    try:
        process = subprocess.run(command, check=False, **kwargs)
    except (OSError, subprocess.SubprocessError) as error:
        raise HarnessError(f"{label} could not run: {error}") from error
    if process.returncode:
        stderr = getattr(process, "stderr", b"")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        diagnostic = str(stderr).strip()
        suffix = f": {diagnostic}" if diagnostic else ""
        raise HarnessError(f"{label} failed with status {process.returncode}{suffix}")
    return process


def default_state_root() -> Path:
    base = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state"))
    return base / "milan-parity"


@contextlib.contextmanager
def locked_state(path_value: str | None) -> Iterator[Path]:
    root = ensure_private_directory(Path(path_value) if path_value else default_state_root(),
                                    create=True)
    lock_path = root / "lock"
    try:
        fd = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
    except OSError as error:
        raise HarnessError(f"cannot open parity state lock: {error.strerror or error}") from error
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        work = root / "work"
        if work.exists():
            for child in work.iterdir():
                if child.name.startswith(".partial-"):
                    shutil.rmtree(child) if child.is_dir() else child.unlink()
        yield root
    except OSError as error:
        raise HarnessError(f"parity state filesystem error: {error.strerror or error}") from error
    finally:
        os.close(fd)


def source_identity(repo: Path) -> str:
    repo = repo.expanduser().resolve()
    roots = [repo / "drivers" / "goodix53x5"]
    fixed = [repo / "meson-integration.patch", repo / "scripts" / "build-local.sh"]
    paths = sorted(path for root in roots for path in root.rglob("*") if path.is_file())
    paths.extend(fixed)
    digest = hashlib.sha256()
    try:
        for path in paths:
            if not path.is_file():
                raise HarnessError(f"build identity input is missing: {path}")
            digest.update(str(path.relative_to(repo)).encode("utf-8") + b"\0")
            digest.update(path.read_bytes())
    except OSError as error:
        raise HarnessError(f"cannot calculate build identity: {error.strerror or error}") from error
    return digest.hexdigest()
