"""Private capture, saved-enrollment, and finalization flows."""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import getpass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from typing import Any

from milan_parity_build import validate_build_manifest
from milan_parity_common import (
    HarnessError,
    POLICY,
    RUNTIME_SCHEMA,
    canonical,
    ensure_private_directory,
    run,
    sha256_file,
    write_atomic,
)


RUNTIME_FILE_RE = re.compile(
    r"^runtime-(?P<action>[a-z]+)-(?P<epoch>\d+)-(?P<generation>\d+)-"
    r"(?P<stage>\d+)-(?P<chronology>\d+)-(?P<timestamp>-?\d+)-"
    r"(?P<crc>[0-9a-f]{8})\.json$")


def validated_username(value: str, label: str) -> str:
    if value in {".", ".."} or not re.fullmatch(r"[A-Za-z0-9._-]+", value):
        raise HarnessError(f"{label} username is invalid")
    return value


def _process(command: list[str] | tuple[str, ...], label: str, **kwargs: Any) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=False, **kwargs)
    except (OSError, subprocess.SubprocessError) as error:
        raise HarnessError(f"{label} could not run: {error}") from error


def _encoding(path: Path) -> str:
    name = path.name
    if name.startswith("runtime-") and path.suffix == ".json":
        return "canonical-json"
    if name.startswith("raw12-") and path.suffix == ".pgm":
        return "pgm-u16be-108x88-max4095"
    if path.suffix == ".pgm":
        return "pgm-u8-108x88-max255"
    if name.startswith("template-"):
        return "goodix-milan-template"
    if path.suffix == ".jsonl":
        return "canonical-json-lines"
    if path.suffix == ".json":
        return "canonical-json"
    return "binary"


def artifact_descriptor(root: Path, path: Path) -> dict[str, Any]:
    try:
        if path.is_symlink() or not path.is_file():
            raise HarnessError(f"artifact is not a regular non-symlink file: {path}")
        relative = str(path.relative_to(root))
        size = path.stat().st_size
    except OSError as error:
        raise HarnessError(f"cannot inspect artifact {path}: {error}") from error
    return {"bytes": size, "encoding": _encoding(path), "path": relative,
            "sha256": sha256_file(path)}


def load_runtime_record(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise HarnessError(f"cannot read runtime record {path}: {error}") from error
    if isinstance(value, dict) and value.get("schema") != RUNTIME_SCHEMA:
        raise HarnessError("recreate dump using the current debug build")
    if raw != canonical(value):
        raise HarnessError(f"runtime record is not canonical JSON: {path}")
    if not isinstance(value, dict):
        raise HarnessError(f"runtime record must be an object: {path}")
    return value


def secure_capture_tree(root: Path) -> None:
    try:
        paths = [root, *root.rglob("*")]
        if any(path.lstat().st_uid != os.getuid() for path in paths):
            run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--", str(root)),
                "take ownership of capture")
        for path in paths:
            if path.is_symlink():
                raise HarnessError(f"capture contains a symlink: {path}")
            os.chmod(path, 0o700 if path.is_dir() else 0o600)
    except OSError as error:
        raise HarnessError(f"cannot secure capture tree {root}: {error}") from error


def snapshot(source_value: str, destination: Path, *, missing_is_empty: bool = False) -> None:
    source = Path(source_value).expanduser().resolve()
    if not source.exists():
        if missing_is_empty:
            try:
                destination.mkdir(mode=0o700)
            except OSError as error:
                raise HarnessError(f"cannot create empty snapshot {destination}: {error}") from error
            return
        raise HarnessError(f"snapshot source is unavailable: {source}")
    run(("sudo", "cp", "-a", "--reflink=auto", "--", str(source), str(destination)),
        "copy fingerprint store")
    run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--",
         str(destination)), "take ownership of fingerprint snapshot")


def journal_cursor(unit: str) -> str:
    process = run(("journalctl", "-u", unit, "--show-cursor", "-n", "1", "-o", "cat"),
                  "read journal cursor", stdout=-1, stderr=-1, text=True)
    marker = "-- cursor: "
    if marker not in process.stdout:
        raise HarnessError(f"cannot obtain journal cursor for {unit}")
    return process.stdout.rsplit(marker, 1)[1].strip()


def dump_inventory(source_value: str) -> list[str]:
    root = Path(source_value).expanduser().resolve()
    if not root.is_dir():
        raise HarnessError(f"debug dump directory is unavailable: {root}")
    command = ["find", str(root), "-maxdepth", "1", "-type", "f", "-printf", "%f\n"]
    if not os.access(root, os.R_OK | os.X_OK):
        command.insert(0, "sudo")
    process = run(command, "inventory debug dump", stdout=-1, stderr=-1, text=True)
    names = sorted(line for line in process.stdout.splitlines() if line)
    if any(name != Path(name).name for name in names):
        raise HarnessError("debug dump inventory contains an invalid filename")
    return names


def copy_dump_files(source_value: str, names: list[str], destination: Path) -> None:
    if not names:
        return
    root = Path(source_value).expanduser().resolve()
    try:
        destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    except OSError as error:
        raise HarnessError(f"cannot create captured dump directory: {error}") from error
    for name in names:
        source = root / name
        symlink_command = ["test", "-L", str(source)]
        if not os.access(root, os.R_OK | os.X_OK):
            symlink_command.insert(0, "sudo")
        if _process(symlink_command, "inspect debug artifact").returncode == 0:
            raise HarnessError(f"debug dump source became a symlink: {source}")
        command = ["cp", "--no-dereference", "--reflink=auto", "--", str(source),
                   str(destination / name)]
        if not os.access(source, os.R_OK):
            command.insert(0, "sudo")
        run(command, f"copy debug artifact {name}")


def verify_loaded_driver(build: dict[str, Any], unit: str) -> None:
    library = Path(str(build["library_path"])).resolve()
    pid = run(("systemctl", "show", unit, "--property=MainPID", "--value"),
              "read fprintd PID", stdout=-1, stderr=-1, text=True).stdout.strip()
    if not pid.isdigit() or int(pid) <= 0:
        raise HarnessError(f"{unit} is not running after the operation")
    try:
        loaded = {Path(line.rsplit(None, 1)[1]).resolve()
                  for line in (Path("/proc") / pid / "maps").read_text().splitlines()
                  if "/" in line and not line.rstrip().endswith(" (deleted)")}
    except OSError as error:
        raise HarnessError(f"cannot verify the library loaded by {unit}: {error}") from error
    if library not in loaded:
        raise HarnessError(f"{unit} did not load the manifest library: {library}")


def _validate_new_files(operation: list[str], names: list[str], status: int) -> None:
    runtimes = [name for name in names if RUNTIME_FILE_RE.fullmatch(name)]
    if not runtimes:
        raise HarnessError("debug capture omitted runtime records")
    command = Path(operation[0]).name.lower()
    if "enroll" in command and status == 0:
        for stage in range(1, 13):
            if not any(name.startswith(f"raw12-enroll-stage-{stage}-") for name in names):
                raise HarnessError(f"successful enrollment omitted accepted raw stage {stage}")
            if not any(name.startswith(f"enroll-stage-{stage}-") for name in names):
                raise HarnessError(f"successful enrollment omitted accepted processed stage {stage}")
    elif "identify" in command or "verify" in command:
        action = "identify" if "identify" in command else "verify"
        if (not any(name.startswith(f"raw12-{action}-") for name in names) or
                not any(name.startswith(f"{action}-") for name in names)):
            raise HarnessError(f"{action} capture omitted raw/processed probe")


def run_capture(args: argparse.Namespace) -> None:
    destination = Path(args.campaign).expanduser().resolve()
    if destination.exists() or destination.is_symlink():
        raise HarnessError(f"campaign already exists and will not be overwritten: {destination}")
    ensure_private_directory(destination.parent)
    partial = destination.parent / f".partial-{destination.name}-{os.getpid()}"
    if partial.exists():
        raise HarnessError(f"partial campaign already exists: {partial}")
    build_path = Path(args.build_manifest).expanduser()
    build = validate_build_manifest(build_path, verify_library=True)
    try:
        partial.mkdir(mode=0o700)
        (partial / "before").mkdir(mode=0o700)
        (partial / "after").mkdir(mode=0o700)
    except OSError as error:
        raise HarnessError(f"cannot create capture campaign: {error}") from error
    try:
        if args.fp_store:
            store = Path(args.fp_store).expanduser().resolve()
        else:
            username = validated_username(args.user or getpass.getuser(), "capture")
            store = Path("/var/lib/fprint") / username
        cursor = journal_cursor(args.journal_unit)
        enrolling = "enroll" in Path(args.operation[0]).name.lower()
        snapshot(str(store), partial / "before" / "fprint-store", missing_is_empty=enrolling)
        before_inventory = dump_inventory(args.dump_dir)
        started = dt.datetime.now(dt.timezone.utc).isoformat()
        process = _process(args.operation, "captured operation")
        end_cursor = journal_cursor(args.journal_unit)
        verify_loaded_driver(build, args.journal_unit)
        snapshot(str(store), partial / "after" / "fprint-store")
        after_inventory = dump_inventory(args.dump_dir)
        new_operation_names = sorted(set(after_inventory) - set(before_inventory))
        _validate_new_files(args.operation, new_operation_names, process.returncode)
        dump = partial / "artifacts" / "debug-dump"
        copy_dump_files(args.dump_dir, after_inventory, dump)
        write_atomic(partial / "artifacts" / "driver-build.json", canonical(build))

        command = ["journalctl", f"--after-cursor={cursor}", "-o", "json"]
        if args.journal_unit:
            command.extend(("-u", args.journal_unit))
        journal = run(command, "capture fprintd journal", stdout=-1, stderr=-1, text=True)
        entries = []
        reached_end = cursor == end_cursor
        for line in journal.stdout.splitlines():
            try:
                entry = json.loads(line)
            except json.JSONDecodeError as error:
                raise HarnessError(f"journalctl returned invalid JSON: {error}") from error
            entries.append(entry)
            if entry.get("__CURSOR") == end_cursor:
                reached_end = True
                break
        if not reached_end:
            raise HarnessError("fprintd journal end cursor was not present in the captured range")
        write_atomic(partial / "journal.jsonl", b"".join(canonical(entry) for entry in entries))

        runtime_identities = set()
        build_ids = set()
        generations = set()
        for name in new_operation_names:
            match = RUNTIME_FILE_RE.fullmatch(name)
            if not match:
                continue
            runtime = load_runtime_record(dump / name)
            if runtime.get("schema") != RUNTIME_SCHEMA:
                raise HarnessError(f"unsupported runtime schema in {name}")
            identity = (runtime.get("capture_session_id"), runtime.get("action"),
                        runtime.get("action_epoch_u64"))
            runtime_identities.add(identity)
            build_ids.add(runtime.get("build_id"))
            generations.add(runtime.get("generation_id_u64"))
        if len(runtime_identities) != 1:
            raise HarnessError("capture expected exactly one runtime session/action/epoch")
        if build_ids != {build["build_id"]}:
            raise HarnessError("captured runtime build ID differs from the driver build manifest")
        session, action, epoch = runtime_identities.pop()
        manifest = {
            "artifacts": [artifact_descriptor(partial, path) for path in sorted(
                item for item in partial.rglob("*") if item.is_file() and
                item.name not in {"capture.json", "SHA256SUMS"})],
            "command": args.operation,
            "driver_build_id": build["build_id"],
            "exit_status": process.returncode,
            "generation_ids_u64": sorted(generations),
            "journal_end_cursor": end_cursor,
            "journal_start_cursor": cursor,
            "operation_identity": {"action": action, "action_epoch_u64": epoch,
                                   "capture_session_id": session},
            "policy": POLICY,
            "runtime_schema": RUNTIME_SCHEMA,
            "schema": "milan-parity-capture/v3",
            "started": started,
        }
        write_atomic(partial / "capture.json", canonical(manifest))
        all_artifacts = [artifact_descriptor(partial, path) for path in sorted(
            item for item in partial.rglob("*") if item.is_file() and item.name != "SHA256SUMS")]
        write_atomic(partial / "SHA256SUMS", b"".join(
            f"{item['sha256']}  {item['path']}\n".encode("ascii") for item in all_artifacts))
        secure_capture_tree(partial)
        os.replace(partial, destination)
    except OSError as error:
        raise HarnessError(f"capture filesystem operation failed: {error}") from error
    finally:
        if partial.exists():
            with contextlib.suppress(OSError):
                shutil.rmtree(partial)
    print(f"milan_parity_capture=pass campaign={destination} operation_status={process.returncode}")
    if process.returncode:
        raise SystemExit(process.returncode)


def run_save_enrollments(args: argparse.Namespace) -> None:
    username = validated_username(args.user or getpass.getuser(), "saved-print")
    source = (Path(args.source).expanduser().resolve() if args.source else
              Path("/var/lib/fprint") / username)
    destination = Path(args.output).expanduser().resolve()
    if destination.exists() or destination.is_symlink():
        raise HarnessError(f"output already exists and will not be overwritten: {destination}")
    ensure_private_directory(destination.parent)
    partial = destination.parent / f".partial-{destination.name}-{os.getpid()}"
    try:
        partial.mkdir(mode=0o700)
        run(("sudo", "cp", "-a", "--reflink=auto", "--", str(source),
             str(partial / "prints")), "copy saved fingerprints")
        run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--", str(partial)),
            "take ownership of saved fingerprints")
        secure_capture_tree(partial)
        artifacts = [artifact_descriptor(partial, path) for path in sorted(
            item for item in (partial / "prints").rglob("*") if item.is_file())]
        if not artifacts:
            raise HarnessError(f"no saved fingerprint files found for user {username}")
        manifest = {"artifacts": artifacts, "schema": "milan-parity-saved-enrollments/v2",
                    "source": str(source), "user": username}
        write_atomic(partial / "saved-enrollments.json", canonical(manifest))
        os.replace(partial, destination)
    except OSError as error:
        raise HarnessError(f"cannot save enrollments: {error}") from error
    finally:
        if partial.exists():
            with contextlib.suppress(OSError):
                shutil.rmtree(partial)
    print(f"milan_parity_save_enrollments=pass output={destination} files={len(artifacts)}")


def run_finish_capture(args: argparse.Namespace) -> None:
    dump = ensure_private_directory(Path(args.dump_dir))
    run_save_enrollments(argparse.Namespace(user=args.user, source=None,
                                            output=str(dump / "saved-enrollments-end")))
    secure_capture_tree(dump)
    try:
        runtime_schemas = set()
        for path in sorted(dump.rglob("runtime-*.json")):
            raw = path.read_bytes()
            match = re.search(
                rb'"schema":"(goodix53x5-runtime-debug/v[0-9]+)"', raw)
            if match:
                runtime_schemas.add(match.group(1).decode("ascii"))
        artifacts = [artifact_descriptor(dump, path) for path in sorted(
            item for item in dump.rglob("*") if item.is_file() and
            item.name != "capture-finished.json")]
    except OSError as error:
        raise HarnessError(f"cannot inventory capture for finalization: {error}") from error
    manifest = {"artifacts": artifacts,
                "observed_runtime_schemas": sorted(runtime_schemas),
                "schema": "milan-parity-finished-capture/v2"}
    write_atomic(dump / "capture-finished.json", canonical(manifest))
    print(f"milan_parity_finish_capture=pass directory={dump} files={len(artifacts)}")
