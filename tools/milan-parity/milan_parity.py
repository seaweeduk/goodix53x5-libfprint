#!/usr/bin/env python3
"""Private, corpus-agnostic Milan parity orchestration."""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import fcntl
import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time
from typing import Any, Iterator, Sequence


CORPUS_SCHEMA = "milan-parity-corpus/v1"
CASE_SCHEMA = "milan-parity-case/v1"
RECORD_SCHEMA = "milan-parity-record/v1"
GENERATION_SCHEMA = "milan-parity-generation/v1"
REPORT_SCHEMA = "milan-parity-report/v1"
POLICY = {
    "anti_fake_mode": 1,
    "boundary_policy": "canonical-zero-v1",
    "print_schema": 3,
    "profile": 9,
    "subtype": 12,
}
QUICK_BUDGET = 1 << 30
FULL_BUDGET = 5 << 30
SUCCESS_RETAINED_BUDGET = 100 << 20
UINT32_MAX = (1 << 32) - 1
RUNTIME_FILE_RE = re.compile(
    r"^runtime-(?P<action>[a-z]+)-(?P<epoch>\d+)-(?P<generation>\d+)-"
    r"(?P<stage>\d+)-(?P<timestamp>-?\d+)-(?P<crc>[0-9a-f]{8})\.json$")
TEMPLATE_FILE_RE = re.compile(
    r"^template-(?P<role>probe|final|input|after-match)-(?P<action>[a-z]+)-"
    r"(?P<epoch>\d+)-(?P<generation>\d+)-(?P<stage>\d+)"
    r"(?:-(?P<position>\d+))?-(?P<timestamp>-?\d+)-(?P<crc>[0-9a-f]{8})\.g53m$")
REFERENCE_TXON_RE = re.compile(
    r"^raw12-ref-txon-(?P<timestamp>-?\d+)-(?P<crc>[0-9a-f]{8})\.pgm$")
AUTH_RAW_RE = re.compile(
    r"^raw12-(?P<action>identify|verify)-[^-]+-(?P<timestamp>-?\d+)-"
    r"(?P<crc>[0-9a-f]{8})\.pgm$")
AUTH_PROCESSED_RE = re.compile(
    r"^(?P<action>identify|verify)-[^-]+-(?P<timestamp>-?\d+)-"
    r"(?P<crc>[0-9a-f]{8})\.pgm$")
ENROLL_RAW_RE = re.compile(
    r"^raw12-enroll-(?:retry-)?stage-(?P<stage>\d+)(?:-[a-z-]+)?-"
    r"(?P<timestamp>-?\d+)-(?P<crc>[0-9a-f]{8})\.pgm$")
ENROLL_PROCESSED_RE = re.compile(
    r"^enroll-(?:retry-)?stage-(?P<stage>\d+)(?:-[a-z-]+)?-"
    r"(?P<timestamp>-?\d+)-(?P<crc>[0-9a-f]{8})\.pgm$")


class HarnessError(RuntimeError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("ascii")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> Any:
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise HarnessError(f"cannot read canonical JSON {path}: {error}") from error
    if raw != canonical(value):
        raise HarnessError(f"JSON is not canonical: {path}")
    return value


def load_runtime_json(path: Path) -> Any:
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise HarnessError(f"cannot read runtime JSON {path}: {error}") from error
    expected = canonical(value)
    if raw == expected:
        return value
    sensor = canonical({"sensor_subtype_u16": value.get("sensor_subtype_u16")})[1:-2] + b","
    setup = canonical({"setup_txon_sha256": value.get("setup_txon_sha256")})[1:-2] + b","
    if raw != expected.replace(sensor + setup, setup + sensor, 1):
        raise HarnessError(f"runtime JSON has an unknown encoding: {path}")
    return value


def write_atomic(path: Path, value: bytes, mode: int = 0o600) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    partial = path.with_name(f".{path.name}.partial-{os.getpid()}")
    try:
        fd = os.open(partial, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
        with os.fdopen(fd, "wb") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(partial, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            partial.unlink()


def ensure_private_directory(path: Path, create: bool = False) -> Path:
    path = path.expanduser().resolve()
    if create:
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        metadata = path.stat()
    except OSError as error:
        raise HarnessError(f"private directory is unavailable: {path}: {error}") from error
    if not stat.S_ISDIR(metadata.st_mode):
        raise HarnessError(f"not a directory: {path}")
    if metadata.st_mode & 0o077:
        raise HarnessError(f"private directory must be owner-only (0700): {path}")
    if metadata.st_uid != os.getuid():
        raise HarnessError(f"private directory is not owned by the invoking user: {path}")
    return path


def member(root: Path, relative: str) -> Path:
    candidate = Path(relative)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise HarnessError(f"artifact path must be relative and contained: {relative}")
    unresolved = root / candidate
    current = root
    for part in candidate.parts:
        current = current / part
        if current.is_symlink():
            raise HarnessError(f"artifact path must not traverse a symlink: {relative}")
    resolved = unresolved.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise HarnessError(f"artifact escapes corpus: {relative}") from error
    return resolved


def require_policy(value: Any, where: str) -> None:
    if value != POLICY:
        raise HarnessError(f"{where} policy must equal {canonical(POLICY).decode().strip()}")


def require_keys(value: Any, keys: set[str], where: str) -> None:
    if not isinstance(value, dict):
        raise HarnessError(f"{where} must be an object")
    missing = sorted(keys - value.keys())
    if missing:
        raise HarnessError(f"{where} is missing required fields: {', '.join(missing)}")


def validate_artifact(root: Path, artifact: Any, where: str) -> tuple[str, str, int]:
    require_keys(artifact, {"path", "sha256", "bytes"}, where)
    if not isinstance(artifact["path"], str) or not isinstance(artifact["sha256"], str):
        raise HarnessError(f"{where} path and sha256 must be strings")
    if not isinstance(artifact["bytes"], int) or artifact["bytes"] < 0:
        raise HarnessError(f"{where} bytes must be a non-negative integer")
    path = member(root, artifact["path"])
    try:
        size = path.stat().st_size
    except OSError as error:
        raise HarnessError(f"missing artifact {artifact['path']}: {error}") from error
    digest = sha256_file(path)
    if size != artifact["bytes"] or digest != artifact["sha256"]:
        raise HarnessError(f"artifact identity differs: {artifact['path']}")
    return artifact["path"], digest, size


def validate_record(record: Any, case_id: str, where: str) -> None:
    require_keys(record, {"schema", "case_id", "policy", "phases", "result"}, where)
    if record["schema"] != RECORD_SCHEMA or record["case_id"] != case_id:
        raise HarnessError(f"{where} record identity differs")
    require_policy(record["policy"], where)
    if not isinstance(record["phases"], list) or not record["phases"]:
        raise HarnessError(f"{where} phases must be a non-empty array")
    names: list[str] = []
    for index, phase in enumerate(record["phases"]):
        require_keys(phase, {"name", "outputs"}, f"{where}.phases[{index}]")
        if not isinstance(phase["name"], str) or not isinstance(phase["outputs"], dict):
            raise HarnessError(f"{where}.phases[{index}] is invalid")
        names.append(phase["name"])
    if len(names) != len(set(names)):
        raise HarnessError(f"{where} contains duplicate phase names")
    if not isinstance(record["result"], dict):
        raise HarnessError(f"{where}.result must be an object")
    validate_record_privacy(record, where)


def validate_record_privacy(value: Any, where: str, key: str = "") -> None:
    if isinstance(value, dict):
        for child_key, child in value.items():
            if not isinstance(child_key, str):
                raise HarnessError(f"{where} has a non-string key")
            validate_record_privacy(child, where, child_key)
    elif isinstance(value, list):
        if len(value) > 256:
            raise HarnessError(f"{where} embeds an oversized array; record hashes instead")
        for child in value:
            validate_record_privacy(child, where, key)
    elif isinstance(value, str):
        if len(value) > 1024:
            raise HarnessError(f"{where} embeds oversized text; record a hash instead")
        lowered = key.lower()
        if lowered in {"image_bytes", "template_bytes", "payload", "raw_bytes"}:
            raise HarnessError(f"{where} embeds private bytes in {key}; record a hash instead")
    elif value is not None and not isinstance(value, (bool, int)):
        raise HarnessError(f"{where} contains a non-canonical scalar")


def validate_corpus(root_value: str, mode: str | None = None) -> dict[str, Any]:
    root = ensure_private_directory(Path(root_value))
    manifest = load_json(root / "corpus.json")
    require_keys(manifest, {"schema", "policy", "cases", "modes"}, "corpus")
    if manifest["schema"] != CORPUS_SCHEMA:
        raise HarnessError(f"unsupported corpus schema: {manifest['schema']}")
    require_policy(manifest["policy"], "corpus")
    if not isinstance(manifest["cases"], list) or not manifest["cases"]:
        raise HarnessError("corpus cases must be a non-empty array")
    case_by_id: dict[str, dict[str, Any]] = {}
    identity_rows: list[Any] = []
    for position, descriptor in enumerate(manifest["cases"]):
        require_keys(descriptor, {"id", "input", "coverage"}, f"cases[{position}]")
        case_id = descriptor["id"]
        if (not isinstance(case_id, str) or not re.fullmatch(r"[A-Za-z0-9._-]+", case_id) or
                case_id in case_by_id or
                not isinstance(descriptor["coverage"], list)):
            raise HarnessError(f"invalid or duplicate case descriptor at position {position}")
        input_path = member(root, descriptor["input"])
        case = load_json(input_path)
        require_keys(case, {"schema", "id", "operation", "order", "policy",
                            "device", "artifacts"}, f"case {case_id}")
        if case["schema"] != CASE_SCHEMA or case["id"] != case_id:
            raise HarnessError(f"case identity differs: {case_id}")
        require_policy(case["policy"], f"case {case_id}")
        if case["operation"] not in {"enroll", "verify", "identify", "study",
                                      "persistence", "cancellation"}:
            raise HarnessError(f"case {case_id} has an unsupported operation")
        if not isinstance(case["order"], int) or case["order"] < 0:
            raise HarnessError(f"case {case_id} order must be a non-negative integer")
        require_keys(case["device"], {"usb_product", "chip_id", "width", "height"},
                     f"case {case_id}.device")
        if case["device"]["width"] != 108 or case["device"]["height"] != 88:
            raise HarnessError(f"case {case_id} dimensions must be 108x88")
        if not isinstance(case["artifacts"], dict) or not case["artifacts"]:
            raise HarnessError(f"case {case_id} artifacts must be a non-empty object")
        artifact_rows = []
        for name in sorted(case["artifacts"]):
            artifact_rows.append((name, *validate_artifact(
                root, case["artifacts"][name], f"case {case_id}.artifacts.{name}")))
        input_digest = sha256_file(input_path)
        identity_rows.append({"id": case_id, "input": descriptor["input"],
                              "input_sha256": input_digest, "artifacts": artifact_rows})
        case_by_id[case_id] = {"descriptor": descriptor, "input": case,
                               "input_path": input_path}
    if not isinstance(manifest["modes"], dict):
        raise HarnessError("corpus modes must be an object")
    for name in ("quick", "full"):
        selected = manifest["modes"].get(name)
        if not isinstance(selected, list) or not selected:
            raise HarnessError(f"corpus mode {name} must select at least one case")
        if len(selected) != len(set(selected)) or any(item not in case_by_id for item in selected):
            raise HarnessError(f"corpus mode {name} contains unknown or duplicate cases")
    orders = [case_by_id[item]["input"]["order"] for item in manifest["modes"]["full"]]
    if orders != sorted(orders):
        raise HarnessError("full case order differs from declared operation order")
    if set(manifest["modes"]["full"]) != set(case_by_id):
        raise HarnessError("full mode must contain every corpus case")
    if mode and mode not in manifest["modes"]:
        raise HarnessError(f"corpus does not define mode: {mode}")
    input_identity = {"schema": CORPUS_SCHEMA, "policy": POLICY, "cases": identity_rows}
    return {"root": root, "manifest": manifest, "cases": case_by_id,
            "input_sha256": sha256_bytes(canonical(input_identity))}


def active_generation(corpus: dict[str, Any]) -> tuple[Path, dict[str, Any]]:
    pointer = corpus["root"] / "generations" / "ACTIVE"
    try:
        generation_id = pointer.read_text(encoding="ascii").strip()
    except OSError as error:
        raise HarnessError(f"active generation is unavailable: {error}") from error
    if not generation_id or generation_id != Path(generation_id).name:
        raise HarnessError("active generation pointer is invalid")
    root = member(corpus["root"], f"generations/{generation_id}")
    generation = load_json(root / "generation.json")
    require_keys(generation, {"schema", "generation_id", "corpus_input_sha256",
                              "policy", "authority_model", "dll", "native_runner",
                              "native_provenance", "native_repetitions", "records"},
                 "generation")
    if generation["schema"] != GENERATION_SCHEMA or generation["generation_id"] != generation_id:
        raise HarnessError("active generation identity differs")
    if generation["corpus_input_sha256"] != corpus["input_sha256"]:
        raise HarnessError("active generation was produced from different corpus inputs")
    require_policy(generation["policy"], "generation")
    if generation["authority_model"] != "canonical-zero-layered-v1":
        raise HarnessError("generation authority model is unsupported")
    if not isinstance(generation["native_repetitions"], int) or generation["native_repetitions"] < 2:
        raise HarnessError("generation did not prove native repeat determinism")
    expected_ids = set(corpus["cases"])
    if set(generation["records"]) != expected_ids:
        raise HarnessError("generation record set differs from corpus case set")
    for case_id, artifact in generation["records"].items():
        validate_artifact(corpus["root"], artifact, f"generation.records.{case_id}")
        record = load_json(member(corpus["root"], artifact["path"]))
        validate_record(record, case_id, f"generation record {case_id}")
    return root, generation


def difference(expected: Any, actual: Any, path: str = "$", all_differences: bool = False,
               output: list[dict[str, Any]] | None = None) -> list[dict[str, Any]]:
    found = output if output is not None else []
    if type(expected) is not type(actual):
        found.append({"field": path, "expected": expected, "actual": actual,
                      "reason": "type"})
        return found
    if isinstance(expected, dict):
        for key in expected:
            child = f"{path}.{key}"
            if key not in actual:
                found.append({"field": child, "expected": expected[key],
                              "actual": "<missing>", "reason": "missing"})
            else:
                difference(expected[key], actual[key], child, all_differences, found)
            if found and not all_differences:
                return found
        for key in actual:
            if key not in expected:
                found.append({"field": f"{path}.{key}", "expected": "<missing>",
                              "actual": actual[key], "reason": "unexpected"})
                if not all_differences:
                    return found
    elif isinstance(expected, list):
        limit = min(len(expected), len(actual))
        for index in range(limit):
            difference(expected[index], actual[index], f"{path}[{index}]",
                       all_differences, found)
            if found and not all_differences:
                return found
        if len(expected) != len(actual):
            found.append({"field": path, "expected": len(expected), "actual": len(actual),
                          "reason": "length"})
    elif expected != actual:
        found.append({"field": path, "expected": expected, "actual": actual,
                      "reason": "value"})
    return found


def runner_digest(runner: Path) -> str:
    if not runner.is_file() or not os.access(runner, os.X_OK):
        raise HarnessError(f"runner is absent or not executable: {runner}")
    return sha256_file(runner)


def native_provenance(runner: Path, dll: Path) -> dict[str, Any]:
    process = subprocess.run((str(runner), "--provenance", "--dll", str(dll)),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if process.returncode:
        diagnostic = process.stderr.decode("utf-8", "replace").strip()
        raise HarnessError(f"native runner provenance failed: {diagnostic}")
    try:
        value = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise HarnessError(f"native runner provenance is invalid JSON: {error}") from error
    if process.stdout != canonical(value):
        raise HarnessError("native runner provenance is not canonical JSON")
    require_keys(value, {"schema", "policy", "authority_commit", "dll_sha256",
                         "native_source_sha256", "native_build_sha256", "wine_version",
                         "architecture"}, "native provenance")
    if value["schema"] != "milan-parity-native-provenance/v1":
        raise HarnessError("native runner provenance schema is unsupported")
    require_policy(value["policy"], "native provenance")
    if value["dll_sha256"] != sha256_file(dll):
        raise HarnessError("native runner provenance DLL identity differs")
    return value


def execute_runner(runner: Path, corpus: dict[str, Any], case_id: str,
                   repo: Path | None, dll: Path | None) -> dict[str, Any]:
    command = [str(runner), "--corpus", str(corpus["root"]), "--case", case_id]
    if repo is not None:
        command.extend(("--repo", str(repo)))
    if dll is not None:
        command.extend(("--dll", str(dll)))
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             check=False)
    if process.returncode:
        diagnostic = process.stderr.decode("utf-8", "replace").strip()
        raise HarnessError(f"runner failed for {case_id} with status {process.returncode}: {diagnostic}")
    try:
        record = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise HarnessError(f"runner returned invalid JSON for {case_id}: {error}") from error
    if process.stdout != canonical(record):
        raise HarnessError(f"runner returned non-canonical JSON for {case_id}")
    validate_record(record, case_id, f"runner output {case_id}")
    return record


def default_state_root() -> Path:
    base = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state"))
    return base / "milan-parity"


@contextlib.contextmanager
def locked_state(path_value: str | None) -> Iterator[Path]:
    root = ensure_private_directory(Path(path_value) if path_value else default_state_root(),
                                    create=True)
    lock_path = root / "lock"
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        cleanup_partials(root)
        yield root
    finally:
        os.close(fd)


def cleanup_partials(root: Path) -> None:
    work = root / "work"
    if not work.exists():
        return
    for child in work.iterdir():
        if child.name.startswith(".partial-"):
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()


def report_path(state: Path, command: str, mode: str, report: dict[str, Any]) -> Path:
    digest = sha256_bytes(canonical(report))[:16]
    return state / "reports" / f"{command}-{mode}-{digest}.json"


def run_verify(args: argparse.Namespace, native: bool = False) -> None:
    mode = "quick" if getattr(args, "quick", False) else "full"
    corpus = validate_corpus(args.corpus, mode)
    runner = Path(args.native_runner if native else args.current_runner).expanduser().resolve()
    repo = None if native else Path(args.repo).expanduser().resolve()
    dll = Path(args.dll).expanduser().resolve() if native else None
    if native:
        if not dll.is_file() or sha256_file(dll) not in set(args.approved_dll_sha256):
            raise HarnessError(f"DLL is absent or has an unapproved SHA-256: {dll}")
    selected = corpus["manifest"]["modes"][mode]
    with locked_state(args.state_root) as state:
        started = time.monotonic()
        if native and args.write_new_generation:
            create_generation(args, corpus, runner, dll, state)
            return
        _, generation = active_generation(corpus)
        if native:
            if generation["dll"]["sha256"] != sha256_file(dll):
                raise HarnessError("selected DLL differs from active generation")
            if generation["native_runner"]["sha256"] != runner_digest(runner):
                raise HarnessError("native runner differs from active generation")
            if generation.get("native_provenance") != native_provenance(runner, dll):
                raise HarnessError("native runner build/Wine provenance differs from active generation")
        cases: list[dict[str, Any]] = []
        first_difference: dict[str, Any] | None = None
        for case_id in selected:
            expected_artifact = generation["records"][case_id]
            expected = load_json(member(corpus["root"], expected_artifact["path"]))
            actual = execute_runner(runner, corpus, case_id, repo, dll)
            differences = difference(expected, actual, all_differences=args.all_differences)
            if native:
                for repetition in range(1, args.repetitions):
                    repeated = execute_runner(runner, corpus, case_id, repo, dll)
                    repeated_differences = difference(
                        actual, repeated, all_differences=args.all_differences)
                    if repeated_differences:
                        for item in repeated_differences:
                            item["reason"] = f"native-repeat-{repetition + 1}-{item['reason']}"
                        differences = repeated_differences
                        break
            case_report = {"case_id": case_id, "equal": not differences,
                           "differences": differences}
            cases.append(case_report)
            if differences and first_difference is None:
                phase = phase_for_field(expected, differences[0]["field"])
                first_difference = {"case_id": case_id, "phase": phase, **differences[0]}
                if not args.all_differences:
                    break
        report = {"schema": REPORT_SCHEMA, "command": "verify-native" if native else "verify",
                  "mode": mode, "policy": POLICY, "corpus_input_sha256": corpus["input_sha256"],
                  "generation_id": generation["generation_id"], "cases": cases,
                  "equal": first_difference is None, "first_difference": first_difference}
        path = report_path(state, report["command"], mode, report)
        write_atomic(path, canonical(report))
        metadata = {"report": str(path), "elapsed_ms": round((time.monotonic() - started) * 1000)}
        write_atomic(path.with_suffix(".run.json"), canonical(metadata))
        if first_difference:
            item = first_difference
            print(f"case={item['case_id']} phase={item['phase']} field={item['field']} "
                  f"expected={json.dumps(item['expected'], ensure_ascii=True)} "
                  f"actual={json.dumps(item['actual'], ensure_ascii=True)} report={path}",
                  file=sys.stderr)
            raise SystemExit(1)
        print(f"milan_parity=pass mode={mode} cases={len(cases)} report={path}")


def phase_for_field(record: dict[str, Any], field: str) -> str:
    if field.startswith("$.phases["):
        try:
            index = int(field.split("[", 1)[1].split("]", 1)[0])
            return record["phases"][index]["name"]
        except (ValueError, IndexError, KeyError):
            pass
    return "result"


def create_generation(args: argparse.Namespace, corpus: dict[str, Any], runner: Path,
                      dll: Path, state: Path) -> None:
    digest = runner_digest(runner)
    provenance = native_provenance(runner, dll)
    generation_id = args.generation_id or dt.datetime.now(dt.timezone.utc).strftime(
        "%Y%m%dT%H%M%SZ") + "-" + corpus["input_sha256"][:12]
    if generation_id != Path(generation_id).name:
        raise HarnessError("generation ID must be a single path component")
    generations = corpus["root"] / "generations"
    generations.mkdir(mode=0o700, exist_ok=True)
    destination = generations / generation_id
    if destination.exists():
        raise HarnessError(f"generation already exists and will not be overwritten: {destination}")
    partial = generations / f".partial-{generation_id}-{os.getpid()}"
    partial.mkdir(mode=0o700)
    records: dict[str, Any] = {}
    try:
        for case_id in corpus["manifest"]["modes"]["full"]:
            record = execute_runner(runner, corpus, case_id, None, dll)
            for repetition in range(1, args.repetitions):
                repeated = execute_runner(runner, corpus, case_id, None, dll)
                repeated_differences = difference(record, repeated)
                if repeated_differences:
                    item = repeated_differences[0]
                    raise HarnessError(
                        f"native repetition {repetition + 1} differs for {case_id} at "
                        f"{item['field']}")
            relative = f"generations/{generation_id}/records/{case_id}.json"
            data = canonical(record)
            target = partial / "records" / f"{case_id}.json"
            write_atomic(target, data)
            records[case_id] = {"path": relative, "sha256": sha256_bytes(data),
                                "bytes": len(data)}
        generation = {"schema": GENERATION_SCHEMA, "generation_id": generation_id,
                      "corpus_input_sha256": corpus["input_sha256"], "policy": POLICY,
                      "authority_model": "canonical-zero-layered-v1",
                      "dll": {"path_hint": dll.name, "sha256": sha256_file(dll)},
                      "native_runner": {"path_hint": runner.name, "sha256": digest},
                      "native_provenance": provenance,
                      "native_repetitions": args.repetitions,
                      "records": records}
        write_atomic(partial / "generation.json", canonical(generation))
        os.replace(partial, destination)
    finally:
        if partial.exists():
            shutil.rmtree(partial)
    if args.promote:
        write_atomic(generations / "ACTIVE", (generation_id + "\n").encode("ascii"))
    print(f"milan_parity_generation=pass generation={generation_id} promoted={int(args.promote)} "
          f"path={destination}")


def disk_usage(path: Path) -> int:
    total = 0
    if not path.exists():
        return 0
    for root, _, files in os.walk(path):
        for name in files:
            with contextlib.suppress(OSError):
                total += (Path(root) / name).stat().st_size
    return total


def run_doctor(args: argparse.Namespace) -> None:
    corpus = validate_corpus(args.corpus)
    corpus_bytes = disk_usage(corpus["root"])
    state = ensure_private_directory(Path(args.state_root) if args.state_root else default_state_root(),
                                     create=True)
    free = shutil.disk_usage(state).free
    result = {"schema": "milan-parity-doctor/v1", "corpus_bytes": corpus_bytes,
              "sealed_prefix_bytes": disk_usage(Path(args.sealed_prefix).resolve())
              if args.sealed_prefix else 0,
              "build_cache_bytes": disk_usage(state / "builds"),
              "quick_peak_estimate": QUICK_BUDGET, "full_peak_estimate": FULL_BUDGET,
              "successful_retained_limit": SUCCESS_RETAINED_BUDGET, "available_bytes": free,
              "quick_ready": free >= QUICK_BUDGET + (512 << 20),
              "full_ready": free >= FULL_BUDGET + (512 << 20)}
    print(canonical(result).decode("ascii"), end="")


def run_gc(args: argparse.Namespace) -> None:
    with locked_state(args.state_root) as state:
        removed: list[str] = []
        for name in ("work", "wine-clones"):
            target = state / name
            if target.exists():
                shutil.rmtree(target)
                removed.append(name)
        builds = state / "builds"
        if builds.exists():
            ordered = sorted((item for item in builds.iterdir() if item.is_dir()),
                             key=lambda item: item.stat().st_mtime, reverse=True)
            for stale in ordered[2:]:
                shutil.rmtree(stale)
                removed.append(str(stale.relative_to(state)))
        reports = state / "reports"
        if reports.exists():
            ordered_reports = sorted((item for item in reports.glob("*.json")
                                      if not item.name.endswith(".run.json")),
                                     key=lambda item: item.stat().st_mtime, reverse=True)
            for stale in ordered_reports[10:]:
                stale.unlink()
                with contextlib.suppress(FileNotFoundError):
                    stale.with_suffix(".run.json").unlink()
                removed.append(str(stale.relative_to(state)))
        if args.failures:
            target = state / "failures"
            if target.exists():
                shutil.rmtree(target)
                removed.append("failures")
        print(f"milan_parity_gc=pass removed={len(removed)} state={state}")


def run_capture(args: argparse.Namespace) -> None:
    destination = Path(args.campaign).expanduser().resolve()
    if destination.exists():
        raise HarnessError(f"campaign already exists and will not be overwritten: {destination}")
    ensure_private_directory(destination.parent)
    campaign = destination.parent / f".partial-{destination.name}-{os.getpid()}"
    if campaign.exists():
        raise HarnessError(f"partial campaign already exists: {campaign}")
    campaign.mkdir(mode=0o700)
    before = campaign / "before"
    after = campaign / "after"
    before.mkdir(mode=0o700)
    after.mkdir(mode=0o700)
    build_manifest = Path(args.build_manifest).expanduser().resolve()
    build_identity = load_json(build_manifest)
    if build_identity.get("schema") != "milan-parity-driver-build/v1" or not build_identity.get(
            "debug_enabled"):
        raise HarnessError("capture requires a canonical debug driver build manifest")
    fp_user = args.user or getpass.getuser()
    fp_store = (Path(args.fp_store).expanduser().resolve() if args.fp_store else
                Path("/var/lib/fprint") / fp_user)
    cursor = journal_cursor(args.journal_unit)
    enrolling = "enroll" in Path(args.operation[0]).name.lower()
    snapshot(str(fp_store), before / "fprint-store", missing_is_empty=enrolling)
    dump_before = dump_inventory(args.dump_dir)
    command_started = dt.datetime.now(dt.timezone.utc).isoformat()
    process = subprocess.run(args.operation, check=False)
    end_cursor = journal_cursor(args.journal_unit)
    verify_loaded_driver(build_identity, args.journal_unit)
    snapshot(str(fp_store), after / "fprint-store")
    dump_after = dump_inventory(args.dump_dir)
    new_dump_files = sorted(set(dump_after) - set(dump_before))
    validate_capture_files(args.operation, new_dump_files, process.returncode)
    copy_dump_files(args.dump_dir, new_dump_files, campaign / "artifacts" / "debug-dump")
    secure_capture_tree(campaign)
    journal_command = ["journalctl", f"--after-cursor={cursor}", "-o", "json"]
    if args.journal_unit:
        journal_command.extend(("-u", args.journal_unit))
    journal = subprocess.run(journal_command, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, check=True, text=True)
    selected_entries = []
    reached_end = cursor == end_cursor
    for line in journal.stdout.splitlines():
        entry = json.loads(line)
        selected_entries.append(entry)
        if entry.get("__CURSOR") == end_cursor:
            reached_end = True
            break
    if not reached_end:
        raise HarnessError("fprintd journal end cursor was not present in the captured range")
    journal_data = b"".join(canonical(entry) for entry in selected_entries)
    write_atomic(campaign / "journal.jsonl", journal_data)
    diagnostic_lines = [str(entry.get("MESSAGE", "")) for entry in selected_entries
                        if "diagnostic[" in str(entry.get("MESSAGE", ""))]
    identities = sorted(set(re.findall(r"epoch=(\d+) generation=(\d+)", "\n".join(
        diagnostic_lines))))
    if len(identities) != 1:
        raise HarnessError(
            f"capture expected one diagnostic epoch/generation, observed {len(identities)}")
    actions = sorted(set(re.findall(r"diagnostic\[([^]]+)\]", "\n".join(diagnostic_lines))))
    expected_action = next((name for name in ("enroll", "identify", "verify")
                            if name in Path(args.operation[0]).name.lower()), None)
    if expected_action and actions != [expected_action]:
        raise HarnessError(
            f"captured diagnostic action differs: expected {expected_action}, observed {actions}")
    manifest = {"schema": "milan-parity-capture/v1", "policy": POLICY,
                "command": args.operation, "started": command_started,
                "exit_status": process.returncode, "journal_start_cursor": cursor,
                "journal_end_cursor": end_cursor,
                "diagnostic_identity": {"action_epoch_u64": int(identities[0][0]),
                                        "generation_id_u64": int(identities[0][1])},
                "diagnostic_line_count": len(diagnostic_lines),
                "new_dump_files": new_dump_files,
                "driver_build": build_identity}
    write_atomic(campaign / "capture.json", canonical(manifest))
    artifacts = []
    for path in sorted(item for item in campaign.rglob("*") if item.is_file() and
                       item.name != "SHA256SUMS"):
        relative = str(path.relative_to(campaign))
        artifacts.append({"path": relative, "sha256": sha256_file(path),
                          "bytes": path.stat().st_size})
    write_atomic(campaign / "SHA256SUMS", b"".join(
        f"{item['sha256']}  {item['path']}\n".encode("utf-8") for item in artifacts))
    secure_capture_tree(campaign)
    os.replace(campaign, destination)
    print(f"milan_parity_capture=pass campaign={destination} operation_status={process.returncode}")
    if process.returncode:
        raise SystemExit(process.returncode)


def snapshot(source_value: str | None, destination: Path, *,
             missing_is_empty: bool = False) -> None:
    if not source_value:
        return
    source = Path(source_value).expanduser().resolve()
    if not source.exists():
        if missing_is_empty:
            destination.mkdir(mode=0o700)
            return
        raise HarnessError(f"snapshot source is unavailable: {source}")
    command = ["cp", "-a", "--reflink=auto", "--", str(source), str(destination)]
    if not os.access(source, os.R_OK):
        command.insert(0, "sudo")
    subprocess.run(command, check=True)


def journal_cursor(unit: str) -> str:
    process = subprocess.run(("journalctl", "-u", unit, "--show-cursor", "-n", "1", "-o", "cat"),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
                             text=True)
    marker = "-- cursor: "
    if marker not in process.stdout:
        raise HarnessError(f"cannot obtain journal cursor for {unit}")
    return process.stdout.rsplit(marker, 1)[1].strip()


def validate_capture_files(operation: list[str], names: list[str], status: int) -> None:
    command = Path(operation[0]).name.lower()
    runtime = [name for name in names if name.startswith("runtime-") and name.endswith(".json")]
    raw_references = [name for name in names if name.startswith("raw12-ref-")]
    if not runtime or not raw_references:
        raise HarnessError("debug capture omitted runtime records or raw reference frames")
    if "enroll" in command and status == 0:
        for stage in range(1, 9):
            if not any(name.startswith(f"raw12-enroll-stage-{stage}-") for name in names):
                raise HarnessError(f"successful enrollment omitted accepted raw stage {stage}")
            if not any(name.startswith(f"enroll-stage-{stage}-") for name in names):
                raise HarnessError(f"successful enrollment omitted accepted processed stage {stage}")
        if len(runtime) < 8:
            raise HarnessError("successful enrollment omitted runtime stage records")
    elif "verify" in command or "identify" in command:
        action = "identify" if "identify" in command else "verify"
        if not any(name.startswith(f"raw12-{action}-") for name in names) or not any(
                name.startswith(f"{action}-") and not name.startswith("raw12-")
                for name in names):
            raise HarnessError(
                f"{action} capture omitted raw/processed probe; configure dump probes=all")


def verify_loaded_driver(build: dict[str, Any], unit: str) -> None:
    library = Path(build.get("library_path", "")).resolve()
    if not library.is_file() or sha256_file(library) != build.get("library_sha256"):
        raise HarnessError("installed libfprint differs from the driver build manifest")
    pid_value = subprocess.run(("systemctl", "show", unit, "--property=MainPID", "--value"),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               check=True, text=True).stdout.strip()
    if not pid_value.isdigit() or int(pid_value) <= 0:
        raise HarnessError(f"{unit} is not running after the operation")
    maps_path = Path("/proc") / pid_value / "maps"
    try:
        loaded_paths = {Path(line.rsplit(None, 1)[1]).resolve()
                        for line in maps_path.read_text().splitlines()
                        if "/" in line and not line.rstrip().endswith(" (deleted)")}
    except OSError as error:
        raise HarnessError(f"cannot verify the library loaded by {unit}: {error}") from error
    if library not in loaded_paths:
        raise HarnessError(f"{unit} did not load the manifest library: {library}")


def dump_inventory(source_value: str | None) -> list[str]:
    if not source_value:
        return []
    root = Path(source_value).expanduser().resolve()
    if not root.is_dir():
        raise HarnessError(f"debug dump directory is unavailable: {root}")
    command = ["find", str(root), "-maxdepth", "1", "-type", "f", "-printf", "%f\n"]
    if not os.access(root, os.R_OK | os.X_OK):
        command.insert(0, "sudo")
    process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             check=True, text=True)
    names = sorted(line for line in process.stdout.splitlines() if line)
    if any(name != Path(name).name for name in names):
        raise HarnessError("debug dump inventory contains an invalid filename")
    return names


def copy_dump_files(source_value: str | None, names: list[str], destination: Path) -> None:
    if not names:
        return
    root = Path(source_value).expanduser().resolve()
    destination.mkdir(mode=0o700, parents=True, exist_ok=True)
    for name in names:
        source = root / name
        target = destination / name
        symlink_check = ["test", "-L", str(source)]
        if not os.access(root, os.R_OK | os.X_OK):
            symlink_check.insert(0, "sudo")
        if subprocess.run(symlink_check, check=False).returncode == 0:
            raise HarnessError(f"debug dump source became a symlink: {source}")
        command = ["cp", "--no-dereference", "--reflink=auto", "--", str(source), str(target)]
        if not os.access(source, os.R_OK):
            command.insert(0, "sudo")
        subprocess.run(command, check=True)


def run_build_manifest(args: argparse.Namespace) -> None:
    repo = Path(args.repo).expanduser().resolve()
    library = Path(args.library).expanduser().resolve()
    if not (repo / ".git").exists() or not library.is_file():
        raise HarnessError("build manifest requires a Git repository and built libfprint library")
    strings = subprocess.run(("strings", str(library)), stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, check=True).stdout
    has_debug_trace = b"goodix53x5-runtime-debug/v1" in strings
    if has_debug_trace != args.debug:
        raise HarnessError("library diagnostic content differs from the requested build mode")
    commit = subprocess.run(("git", "rev-parse", "HEAD"), cwd=repo,
                            stdout=subprocess.PIPE, check=True, text=True).stdout.strip()
    source_digest = hashlib.sha256()
    source_files = sorted((repo / "drivers" / "goodix53x5").glob("*.[ch]"))
    source_files.extend((repo / name for name in ("meson-integration.patch", "scripts/build-local.sh")))
    for path in source_files:
        source_digest.update(str(path.relative_to(repo)).encode("utf-8") + b"\0")
        source_digest.update(path.read_bytes())
    manifest = {"schema": "milan-parity-driver-build/v1", "source_commit": commit,
                "source_sha256": source_digest.hexdigest(),
                "library_path": str(library), "library_sha256": sha256_file(library),
                "library_bytes": library.stat().st_size,
                "debug_enabled": args.debug, "boundary_policy": "canonical-zero-v1",
                "print_schema": 3, "profile": 9, "subtype": 12, "anti_fake_mode": 1}
    output = Path(args.output).expanduser().resolve()
    if output.exists():
        raise HarnessError(f"build manifest already exists and will not be overwritten: {output}")
    write_atomic(output, canonical(manifest))
    print(f"milan_parity_build_manifest=pass output={output}")


def secure_capture_tree(root: Path) -> None:
    foreign_owner = False
    for path in [root, *root.rglob("*")]:
        with contextlib.suppress(OSError):
            if path.lstat().st_uid != os.getuid():
                foreign_owner = True
                break
    if foreign_owner:
        subprocess.run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--", str(root)),
                       check=True)
    for path in [root, *root.rglob("*")]:
        if path.is_symlink():
            raise HarnessError(f"capture contains a symlink and cannot be admitted: {path}")
        os.chmod(path, 0o700 if path.is_dir() else 0o600)


def run_save_enrollments(args: argparse.Namespace) -> None:
    username = args.user or getpass.getuser()
    if not re.fullmatch(r"[A-Za-z0-9._-]+", username):
        raise HarnessError("saved-print username is invalid")
    source = (Path(args.source).expanduser().resolve() if args.source else
              Path("/var/lib/fprint") / username)
    destination = Path(args.output).expanduser().resolve()
    if destination.exists():
        raise HarnessError(f"output already exists and will not be overwritten: {destination}")
    ensure_private_directory(destination.parent)
    partial = destination.parent / f".partial-{destination.name}-{os.getpid()}"
    if partial.exists():
        raise HarnessError(f"partial output already exists: {partial}")
    partial.mkdir(mode=0o700)
    try:
        subprocess.run(("sudo", "cp", "-a", "--reflink=auto", "--", str(source),
                        str(partial / "prints")), check=True)
        subprocess.run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--",
                        str(partial)), check=True)
        secure_capture_tree(partial)
        artifacts = []
        for path in sorted(item for item in (partial / "prints").rglob("*") if item.is_file()):
            artifacts.append({"path": str(path.relative_to(partial)),
                              "sha256": sha256_file(path), "bytes": path.stat().st_size})
        if not artifacts:
            raise HarnessError(f"no saved fingerprint files found for user {username}")
        manifest = {"schema": "milan-parity-saved-enrollments/v1", "user": username,
                    "source": str(source), "artifacts": artifacts}
        write_atomic(partial / "saved-enrollments.json", canonical(manifest))
        os.replace(partial, destination)
    finally:
        if partial.exists():
            shutil.rmtree(partial)
    print(f"milan_parity_save_enrollments=pass output={destination} files={len(artifacts)}")


def run_finish_capture(args: argparse.Namespace) -> None:
    dump = ensure_private_directory(Path(args.dump_dir))
    saved = dump / "saved-enrollments-end"
    namespace = argparse.Namespace(user=args.user, source=None, output=str(saved))
    run_save_enrollments(namespace)
    subprocess.run(("sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}", "--", str(dump)),
                   check=True)
    secure_capture_tree(dump)
    artifacts = []
    for path in sorted(item for item in dump.rglob("*") if item.is_file() and
                       item.name != "capture-finished.json"):
        artifacts.append({"path": str(path.relative_to(dump)),
                          "sha256": sha256_file(path), "bytes": path.stat().st_size})
    manifest = {"schema": "milan-parity-finished-capture/v1", "policy": POLICY,
                "artifacts": artifacts}
    write_atomic(dump / "capture-finished.json", canonical(manifest))
    print(f"milan_parity_finish_capture=pass directory={dump} files={len(artifacts)}")


def pgm_payload(path: Path, raw: bool) -> bytes:
    data = path.read_bytes()
    header = b"P5\n108 88\n4095\n" if raw else b"P5\n108 88\n255\n"
    size = 108 * 88 * (2 if raw else 1)
    if not data.startswith(header) or len(data) != len(header) + size:
        raise HarnessError(f"invalid debug PGM: {path.name}")
    return data[len(header):]


def crc32_mpeg2(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte << 24
        for _ in range(8):
            value = ((value << 1) ^ (0x04C11DB7 if value & 0x80000000 else 0)) & UINT32_MAX
    return value


def verify_dump_crc(path: Path, expected: str, payload: bytes | None = None) -> None:
    actual = crc32_mpeg2(path.read_bytes() if payload is None else payload)
    if actual != int(expected, 16):
        raise HarnessError(f"debug dump CRC differs: {path.name}")


def artifact_with_digest(entries: list[tuple[str, Path]], digest: Any) -> Path | None:
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        return None
    return next((path for actual, path in entries if actual == digest), None)


def native_case_for_dump(state: Path, dump: Path, runtime_path: Path,
                         runtime: dict[str, Any], setup: Path, live: Path,
                         templates: dict[tuple[str, int | None], list[tuple[str, Path]]],
                         dll_sha256: str, wine_prefix: Path) -> dict[str, Any]:
    identity = (runtime["action_epoch_u64"], runtime["generation_id_u64"],
                runtime["stage_u32"])
    root = state / "work" / (
        f"dump-{sha256_bytes(str(dump).encode())[:12]}-{identity[0]}-{identity[1]}-{identity[2]}")
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(mode=0o700, parents=True)
    artifacts = {
        "setup": admit_artifact(root, str(setup), image_format="pgm12"),
        "live": admit_artifact(root, str(live), image_format="pgm12"),
    }
    gallery = []
    for position, observed in enumerate(runtime["gallery"]):
        template = artifact_with_digest(templates.get(("input", position), []),
                                        observed.get("input_template_sha256"))
        if not template:
            raise HarnessError(f"gallery input {position} is missing from the dump")
        name = f"gallery-{position:03d}"
        artifacts[name] = admit_artifact(root, str(template))
        gallery.append({"artifact": name, "index": observed["gallery_index_u32"],
                        "queue_occupied_after_match_u64":
                            observed["queue_occupied_after_u64"]
                            if observed["accepted"] else 0})
    case_id = f"dump-{identity[0]}-{identity[1]}-{identity[2]}"
    replay = {
        "purpose": "identify", "setup": "setup", "live": ["live"],
        "gallery": gallery, "tcode": runtime["tcode_u16"],
        "dac_high": runtime["dac_high_u16"], "dac_low": runtime["dac_low_u16"],
        "native": {"schema": "milan-parity-native-replay/v1",
                   "authority_model": "canonical-zero-layered-v1",
                   "dll_sha256": dll_sha256, "wine_prefix": str(wine_prefix)},
    }
    case = {"schema": CASE_SCHEMA, "id": case_id, "operation": runtime["action"],
            "order": 0, "policy": POLICY, "artifacts": artifacts, "replay": replay,
            "device": {"usb_product": "5335", "chip_id": "dump",
                       "width": 108, "height": 88}}
    case_relative = f"cases/{case_id}/input.json"
    write_atomic(root / case_relative, canonical(case))
    corpus = {"schema": CORPUS_SCHEMA, "policy": POLICY,
              "cases": [{"id": case_id, "input": case_relative,
                         "coverage": ["passive-dump"]}],
              "modes": {"quick": [case_id], "full": [case_id]}}
    write_atomic(root / "corpus.json", canonical(corpus))
    return {"root": root, "case_id": case_id}


def print_dump_summary(report: dict[str, Any], output: Path) -> None:
    operations = report["operations"]
    checks = [check for operation in operations for check in operation["checks"]]
    native = [check for check in checks if check["name"] == "native-parity"]
    native_passes = sum(check["status"] == "pass" for check in native)
    native_failures = sum(check["status"] == "fail" for check in native)
    native_skips = sum(check["status"] == "skipped" for check in native)
    native_status = "fail" if native_failures else ("pass" if native_passes else "not-run")
    print(f"native_parity={native_status} compared={native_passes} "
          f"failed={native_failures} unavailable={native_skips}")

    compared = [operation for operation in operations
                if any(check["name"] == "native-parity" and check["status"] == "pass"
                       for check in operation["checks"])]
    if compared:
        actions: dict[int, int] = {}
        queue_transitions: dict[str, int] = {}
        scores = []
        for operation in compared:
            observed = operation["observed"]
            action = observed["study_action_u32"]
            actions[action] = actions.get(action, 0) + 1
            scores.append(observed["score_i32"])
            for row in observed["gallery"]:
                if row["accepted"]:
                    transition = (f"{row['queue_occupied_before_u64']}->"
                                  f"{row['queue_occupied_after_u64']}")
                    queue_transitions[transition] = queue_transitions.get(transition, 0) + 1
        action_text = ",".join(f"{key}:{actions[key]}" for key in sorted(actions))
        score_text = ",".join(str(score) for score in sorted(scores))
        queue_text = (",".join(f"{key}:{queue_transitions[key]}"
                               for key in sorted(queue_transitions)) or "none")
        print(f"native_coverage actions={action_text} scores={score_text} queues={queue_text}")

    continuity = [check for check in checks if check["name"] == "candidate-reobserved"]
    reobserved = sum(check["status"] == "pass" and
                     check["detail"].startswith("the exact learned candidate")
                     for check in continuity)
    pending = sum(check["status"] == "skipped" for check in continuity)
    continuity_failures = sum(check["status"] == "fail" for check in continuity)
    continuity_status = "fail" if continuity_failures else ("partial" if pending else "pass")
    print(f"learning_continuity={continuity_status} reobserved={reobserved} "
          f"pending={pending} failed={continuity_failures}")

    skip_groups: dict[tuple[str, str], int] = {}
    for check in checks:
        if check["status"] == "skipped":
            key = (check["name"], check["detail"])
            skip_groups[key] = skip_groups.get(key, 0) + 1
    if skip_groups:
        print("skipped_checks:")
        for (name, detail), count in sorted(skip_groups.items(),
                                            key=lambda item: (-item[1], item[0])):
            print(f"  {count} {name}: {detail}")
    else:
        print("skipped_checks=none")
    print(f"report={output}")


def run_validate_dump(args: argparse.Namespace) -> None:
    dump = ensure_private_directory(Path(args.dump_dir))
    runtimes = []
    templates_by_identity: dict[
        tuple[str, int, int, int], dict[tuple[str, int | None], list[tuple[str, Path]]]
    ] = {}
    references = []
    auth_raw: dict[str, list[tuple[str, Path]]] = {"identify": [], "verify": []}
    auth_processed: dict[str, list[tuple[str, Path]]] = {"identify": [], "verify": []}
    enroll_raw: dict[int, list[tuple[str, Path]]] = {}
    enroll_processed: dict[int, list[tuple[str, Path]]] = {}
    reserved_errors = []
    inventory = []
    for path in sorted(dump.iterdir()):
        if not path.is_file() or path.is_symlink():
            continue
        inventory.append({"path": path.name, "bytes": path.stat().st_size,
                          "sha256": sha256_file(path)})
        match = RUNTIME_FILE_RE.fullmatch(path.name)
        if match:
            verify_dump_crc(path, match["crc"])
            value = load_runtime_json(path)
            identity = (match["action"], int(match["epoch"]), int(match["generation"]),
                        int(match["stage"]))
            if (value.get("schema") != "goodix53x5-runtime-debug/v1" or
                    (value.get("action"), value.get("action_epoch_u64"),
                     value.get("generation_id_u64"), value.get("stage_u32")) != identity):
                raise HarnessError(f"runtime identity differs: {path.name}")
            runtimes.append((int(match["timestamp"]), path, value))
            continue
        match = TEMPLATE_FILE_RE.fullmatch(path.name)
        if match:
            verify_dump_crc(path, match["crc"])
            identity = (match["action"], int(match["epoch"]), int(match["generation"]),
                        int(match["stage"]))
            position = int(match["position"]) if match["position"] is not None else None
            templates_by_identity.setdefault(identity, {}).setdefault(
                (match["role"], position), []).append((sha256_file(path), path))
            continue
        match = REFERENCE_TXON_RE.fullmatch(path.name)
        if match:
            payload = pgm_payload(path, raw=True)
            verify_dump_crc(path, match["crc"], payload)
            references.append((sha256_bytes(payload), path))
            continue
        match = AUTH_RAW_RE.fullmatch(path.name)
        if match:
            payload = pgm_payload(path, raw=True)
            verify_dump_crc(path, match["crc"], payload)
            auth_raw[match["action"]].append((sha256_bytes(payload), path))
            continue
        match = ENROLL_RAW_RE.fullmatch(path.name)
        if match:
            payload = pgm_payload(path, raw=True)
            verify_dump_crc(path, match["crc"], payload)
            enroll_raw.setdefault(int(match["stage"]), []).append((sha256_bytes(payload), path))
            continue
        match = AUTH_PROCESSED_RE.fullmatch(path.name)
        if match:
            payload = pgm_payload(path, raw=False)
            verify_dump_crc(path, match["crc"], payload)
            auth_processed[match["action"]].append((sha256_bytes(payload), path))
            continue
        match = ENROLL_PROCESSED_RE.fullmatch(path.name)
        if match:
            payload = pgm_payload(path, raw=False)
            verify_dump_crc(path, match["crc"], payload)
            enroll_processed.setdefault(int(match["stage"]), []).append(
                (sha256_bytes(payload), path))
            continue
        if path.name.startswith(("runtime-", "template-")):
            reserved_errors.append(path.name)
    if reserved_errors:
        raise HarnessError(f"malformed reserved dump files: {', '.join(reserved_errors)}")
    if not runtimes:
        raise HarnessError("dump contains no runtime records")
    inventory_sha256 = sha256_bytes(canonical(inventory))
    templates_enabled = bool(templates_by_identity)
    runtimes.sort(key=lambda item: item[0])
    operations = []
    dll = Path(args.dll).expanduser().resolve() if args.dll else None
    prefix = Path(args.wine_prefix).expanduser().resolve() if args.wine_prefix else None
    if dll and (not args.approved_dll_sha256 or sha256_file(dll) != args.approved_dll_sha256):
        raise HarnessError("selected DLL is absent or not explicitly approved")
    with locked_state(args.state_root) as state:
        for timestamp, runtime_path, runtime in runtimes:
            identity = (runtime["action"], runtime["action_epoch_u64"],
                        runtime["generation_id_u64"], runtime["stage_u32"])
            checks = []
            template_map = templates_by_identity.get(identity, {})
            template_errors = []
            if runtime.get("probe_sha256"):
                probe = artifact_with_digest(template_map.get(("probe", None), []),
                                             runtime["probe_sha256"])
                if not probe:
                    template_errors.append("probe")
            if runtime.get("candidate_sha256"):
                final = artifact_with_digest(template_map.get(("final", None), []),
                                             runtime["candidate_sha256"])
                if not final:
                    template_errors.append("final")
            for position, row in enumerate(runtime.get("gallery", [])):
                for role, field in (("input", "input_template_sha256"),
                                    ("after-match", "after_match_sha256")):
                    expected = row.get(field)
                    artifact = artifact_with_digest(template_map.get((role, position), []),
                                                    expected)
                    if expected and not artifact:
                        template_errors.append(f"{role}-{position}")
            checks.append({"name": "template-integrity",
                           "status": ("skipped" if not templates_enabled else
                                      "fail" if template_errors else "pass"),
                           "detail": ("template dumping was not enabled" if not templates_enabled else
                                      template_errors or "all referenced templates match")})
            native_check = {"name": "native-parity", "status": "skipped", "detail": ""}
            action = runtime["action"]
            live = (artifact_with_digest(auth_raw.get(action, []),
                                         runtime.get("live_raw_sha256"))
                    if action in {"identify", "verify"} else
                    artifact_with_digest(enroll_raw.get(runtime["stage_u32"], []),
                                         runtime.get("live_raw_sha256")))
            processed = (artifact_with_digest(auth_processed.get(action, []),
                                              runtime.get("processed_image_sha256"))
                         if action in {"identify", "verify"} else
                         artifact_with_digest(enroll_processed.get(runtime["stage_u32"], []),
                                              runtime.get("processed_image_sha256")))
            setup = artifact_with_digest(references, runtime.get("setup_txon_sha256"))
            required_metadata = {
                "dac_high_u16", "dac_low_u16", "generation_use_index_u64",
                "live_raw_sha256", "probe_record_count_u32", "processed_image_sha256",
                "profile_u16", "purpose_u32", "sensor_subtype_u16",
                "setup_txon_sha256", "tcode_u16",
            }
            fixed_metadata = {
                "dac_high_u16": 125, "dac_low_u16": 198, "profile_u16": 9,
                "purpose_u32": 0, "sensor_subtype_u16": 12, "tcode_u16": 121,
            }
            if action == "enroll":
                native_check["detail"] = "enrollment authority requires a complete eight-stage chain"
            elif not required_metadata.issubset(runtime):
                native_check["detail"] = "runtime record predates self-contained replay metadata"
            elif runtime["generation_use_index_u64"] != 1:
                native_check["detail"] = "operation requires earlier generation uses for replay"
            elif any(runtime.get(key) != value for key, value in fixed_metadata.items()):
                native_check["detail"] = "operation does not use the fixed native authority metadata"
            elif any(row.get("valid") is not True or row.get("evaluated") is not True
                     for row in runtime.get("gallery", [])):
                native_check["detail"] = "native authority cannot replay invalid or unevaluated gallery rows"
            elif not dll or not prefix:
                native_check["detail"] = "DLL or Wine prefix was not supplied"
            elif (template_errors or not live or not processed or not setup or
                  not runtime.get("gallery")):
                native_check["detail"] = "required setup, live frame, or gallery template is missing"
            else:
                case = native_case_for_dump(state, dump, runtime_path, runtime, setup, live,
                                            template_map, args.approved_dll_sha256, prefix)
                actual = execute_runner(Path(args.native_runner).resolve(),
                                        validate_corpus(str(case["root"])), case["case_id"],
                                        None, dll)
                expected = {
                    "quality": runtime["quality_i32"],
                    "coverage": runtime["coverage_i32"],
                    "processed": sha256_bytes(pgm_payload(processed, raw=False)),
                    "records": runtime["probe_record_count_u32"],
                    "score": runtime["score_i32"],
                    "accepted": runtime["status_u32"] == 0,
                    "winner_index": runtime["winner_index_u32"],
                    "winner_position": min(runtime["winner_position_u64"], UINT32_MAX),
                    "study_action": runtime["study_action_u32"],
                    "candidate": runtime["candidate_sha256"],
                    "gallery": [{"index": row["gallery_index_u32"],
                                 "after_match": row["after_match_sha256"],
                                 "score": row["score_i32"],
                                 "accepted": row["accepted"],
                                 "evaluated": row["evaluated"],
                                 "valid": row["valid"]}
                                for row in runtime["gallery"]],
                }
                native = {
                    "quality": actual["phases"][0]["outputs"]["quality_i32"],
                    "coverage": actual["phases"][0]["outputs"]["coverage_i32"],
                    "processed": actual["phases"][0]["outputs"]["processed_image_sha256"],
                    "records": actual["phases"][1]["outputs"]["active_record_count_u32"],
                    "score": actual["result"]["score_i32"],
                    "accepted": actual["result"]["accepted"],
                    "winner_index": actual["result"]["winner_index_u32"],
                    "winner_position": actual["result"]["winner_position_u32"],
                    "study_action": actual["result"]["study_action_u32"],
                    "candidate": actual["result"]["final_candidate_sha256"],
                    "gallery": [{"index": row["gallery_index_u32"],
                                 "after_match": row["after_match_sha256"],
                                 "score": row["score_i32"],
                                 "accepted": row["accepted"],
                                 "evaluated": row["evaluated"],
                                 "valid": row["valid"]}
                                for row in actual["result"]["gallery"]],
                }
                differences = difference(expected, native, all_differences=True)
                native_check = {"name": "native-parity",
                                "status": "fail" if differences else "pass",
                                "detail": differences or "all comparable native outputs match"}
            checks.append(native_check)
            candidate = runtime.get("candidate_sha256")
            if candidate is None:
                persistence_check = {
                    "name": "candidate-reobserved", "status": "pass",
                    "detail": "operation published no learned candidate"}
            else:
                reloaded = any(
                    later_timestamp > timestamp and
                    any(row.get("input_template_sha256") == candidate
                        for row in later_runtime.get("gallery", []))
                    for later_timestamp, _, later_runtime in runtimes)
                persistence_check = {
                    "name": "candidate-reobserved",
                    "status": "pass" if reloaded else "skipped",
                    "detail": ("the exact learned candidate appears in a later gallery"
                               if reloaded else
                               "no later gallery contains the exact learned candidate")}
            checks.append(persistence_check)
            operations.append({
                "action": action, "action_epoch_u64": runtime["action_epoch_u64"],
                "generation_id_u64": runtime["generation_id_u64"],
                "stage_u32": runtime["stage_u32"], "checks": checks,
                "observed": {
                    "score_i32": runtime["score_i32"],
                    "status_u32": runtime["status_u32"],
                    "study_action_u32": runtime["study_action_u32"],
                    "gallery": [{
                        "accepted": row["accepted"],
                        "queue_occupied_after_u64": row["queue_occupied_after_u64"],
                        "queue_occupied_before_u64": row["queue_occupied_before_u64"],
                    } for row in runtime.get("gallery", [])],
                },
            })
        failures = sum(check["status"] == "fail" for operation in operations
                       for check in operation["checks"])
        passes = sum(check["status"] == "pass" for operation in operations
                     for check in operation["checks"])
        skipped = sum(check["status"] == "skipped" for operation in operations
                      for check in operation["checks"])
        report = {"schema": "milan-parity-dump-report/v1", "policy": POLICY,
                  "inventory_sha256": inventory_sha256,
                  "operations": operations, "summary": {"fail": failures,
                                                          "pass": passes,
                                                          "skipped": skipped}}
        output = Path(args.report).expanduser().resolve() if args.report else (
            state / "reports" / f"dump-{inventory_sha256[:16]}.json")
        write_atomic(output, canonical(report))
        print(f"milan_parity_dump={'fail' if failures else 'pass'} operations={len(operations)} "
              f"checks={passes}/{failures}/{skipped}")
        print_dump_summary(report, output)
        if failures:
            raise SystemExit(1)


def pgm12_to_u16le(source: Path) -> bytes:
    data = source.read_bytes()
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P5" or parts[1] != b"108 88" or parts[2] != b"4095":
        raise HarnessError(f"raw capture is not a 108x88 maxval-4095 binary PGM: {source}")
    pixels = parts[3]
    if len(pixels) != 108 * 88 * 2:
        raise HarnessError(f"raw PGM has an invalid payload size: {source}")
    output = bytearray(len(pixels))
    for index in range(0, len(pixels), 2):
        output[index] = pixels[index + 1]
        output[index + 1] = pixels[index]
    return bytes(output)


def admit_artifact(root: Path, source_value: str, wrap_g53m: bool = False,
                   image_format: str = "binary") -> dict[str, Any]:
    source = Path(source_value).expanduser().resolve()
    if not source.is_file() or source.is_symlink():
        raise HarnessError(f"admission artifact is absent, non-regular, or a symlink: {source}")
    prefix = b"G53M\x03\x00" if wrap_g53m else b""
    converted = pgm12_to_u16le(source) if image_format == "pgm12" else None
    digest = hashlib.sha256(prefix)
    size = len(prefix)
    if converted is not None:
        digest.update(converted)
        size += len(converted)
    else:
        with source.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
                size += len(block)
    sha256 = digest.hexdigest()
    relative = f"objects/sha256/{sha256[:2]}/{sha256}"
    destination = root / relative
    if destination.exists():
        if destination.stat().st_size != size or sha256_file(destination) != sha256:
            raise HarnessError(f"content-addressed object identity collision: {destination}")
    else:
        destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        partial = destination.with_name(f".{destination.name}.partial-{os.getpid()}")
        try:
            with partial.open("xb") as output:
                os.chmod(partial, 0o600)
                output.write(prefix)
                if converted is not None:
                    output.write(converted)
                else:
                    with source.open("rb") as stream:
                        shutil.copyfileobj(stream, output, 1024 * 1024)
                output.flush()
                os.fsync(output.fileno())
            os.replace(partial, destination)
        finally:
            partial.unlink(missing_ok=True)
    return {"path": relative, "sha256": sha256, "bytes": size}


def run_admit(args: argparse.Namespace) -> None:
    if not re.fullmatch(r"[A-Za-z0-9._-]+", args.case_id):
        raise HarnessError("case ID must contain only letters, digits, dot, underscore, or hyphen")
    root = Path(args.corpus).expanduser().resolve()
    if root.exists():
        root = ensure_private_directory(root)
        manifest = load_json(root / "corpus.json")
        require_policy(manifest.get("policy"), "corpus")
        if manifest.get("schema") != CORPUS_SCHEMA:
            raise HarnessError("existing corpus schema is unsupported")
    else:
        root.mkdir(mode=0o700, parents=True)
        manifest = {"schema": CORPUS_SCHEMA, "policy": POLICY, "cases": [],
                    "modes": {"quick": [], "full": []}}
    if (not 0 <= args.order or not 0 <= args.tcode <= 0xFFFF or
            not 0 <= args.dac_high <= 0xFFFF or not 0 <= args.dac_low <= 0xFFFF):
        raise HarnessError("order and replay metadata are out of range")
    if not re.fullmatch(r"[0-9a-f]{64}", args.dll_sha256):
        raise HarnessError("DLL SHA-256 must be 64 lowercase hexadecimal characters")
    if any(item.get("id") == args.case_id for item in manifest["cases"]):
        raise HarnessError(f"case already exists and will not be overwritten: {args.case_id}")
    existing_orders = []
    for descriptor in manifest["cases"]:
        existing_case = load_json(member(root, descriptor["input"]))
        existing_orders.append(existing_case.get("order"))
    if existing_orders and args.order < existing_orders[-1]:
        raise HarnessError("new case order would make full mode non-chronological")
    artifacts: dict[str, Any] = {
        "setup": admit_artifact(root, args.setup, image_format=args.image_format)}
    prelude_names = []
    for index, path in enumerate(args.identify_prelude):
        name = f"prelude-{index:03d}"
        artifacts[name] = admit_artifact(root, path, image_format=args.image_format)
        prelude_names.append(name)
    live_names = []
    for index, path in enumerate(args.live):
        name = f"live-{index:03d}"
        artifacts[name] = admit_artifact(root, path, image_format=args.image_format)
        live_names.append(name)
    gallery = []
    for position, value in enumerate(args.gallery):
        try:
            index_value, path = value.split("=", 1)
            gallery_index = int(index_value, 10)
        except (ValueError, TypeError) as error:
            raise HarnessError("gallery must use INDEX=PATH") from error
        if not 0 <= gallery_index <= 0xFFFFFFFF:
            raise HarnessError("gallery index must be a uint32")
        name = f"gallery-{position:03d}"
        artifacts[name] = admit_artifact(root, path, wrap_g53m=args.gallery_format == "raw-packed")
        gallery.append({"index": gallery_index, "artifact": name})
    if args.operation == "enroll" and gallery:
        raise HarnessError("enrollment admission must not supply a gallery")
    if args.operation != "enroll" and len(live_names) != 1:
        raise HarnessError("verify/identify/study admission requires exactly one live frame")
    if args.operation != "enroll" and not gallery:
        raise HarnessError("verify/identify/study admission requires an explicit gallery")
    native = {"schema": "milan-parity-native-replay/v1",
              "authority_model": "canonical-zero-layered-v1",
              "dll_sha256": args.dll_sha256}
    if args.wine_prefix:
        native["wine_prefix"] = str(Path(args.wine_prefix).expanduser().resolve())
    replay = {"purpose": "enroll" if args.operation == "enroll" else "identify",
              "setup": "setup", "live": live_names, "gallery": gallery,
              "tcode": args.tcode, "dac_high": args.dac_high, "dac_low": args.dac_low,
              "native": native}
    if prelude_names:
        replay["prelude"] = prelude_names
    case = {"schema": CASE_SCHEMA, "id": args.case_id, "operation": args.operation,
            "order": args.order, "policy": POLICY,
            "device": {"usb_product": args.usb_product, "chip_id": args.chip_id,
                       "width": 108, "height": 88},
            "artifacts": artifacts, "replay": replay}
    case_relative = f"cases/{args.case_id}/input.json"
    case_path = root / case_relative
    if case_path.exists():
        raise HarnessError(f"case path already exists and will not be overwritten: {case_path}")
    write_atomic(case_path, canonical(case))
    manifest["cases"].append({"id": args.case_id, "input": case_relative,
                              "coverage": sorted(set(args.coverage))})
    manifest["modes"]["full"].append(args.case_id)
    if args.quick:
        manifest["modes"]["quick"].append(args.case_id)
    if not manifest["modes"]["quick"]:
        manifest["modes"]["quick"].append(args.case_id)
    write_atomic(root / "corpus.json", canonical(manifest))
    validate_corpus(str(root))
    print(f"milan_parity_admit=pass case={args.case_id} corpus={root}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="milan-parity")
    commands = parser.add_subparsers(dest="command", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("--repo", required=True)
    verify.add_argument("--corpus", required=True)
    verify.add_argument("--current-runner", required=True)
    verify.add_argument("--state-root")
    modes = verify.add_mutually_exclusive_group(required=True)
    modes.add_argument("--quick", action="store_true")
    modes.add_argument("--full", action="store_true")
    verify.add_argument("--all-differences", action="store_true")

    native = commands.add_parser("verify-native")
    native.add_argument("--corpus", required=True)
    native.add_argument("--dll", required=True)
    native.add_argument("--native-runner", required=True)
    native.add_argument("--approved-dll-sha256", action="append", required=True)
    native.add_argument("--state-root")
    native_modes = native.add_mutually_exclusive_group()
    native_modes.add_argument("--quick", action="store_true")
    native_modes.add_argument("--full", action="store_true")
    native.set_defaults(full=True)
    native.add_argument("--all-differences", action="store_true")
    native.add_argument("--write-new-generation", action="store_true")
    native.add_argument("--generation-id")
    native.add_argument("--promote", action="store_true")
    native.add_argument("--repetitions", type=int, default=2)

    capture = commands.add_parser("capture")
    capture.add_argument("--campaign", required=True)
    capture.add_argument("--user", help="Fingerprint owner; defaults to the current user")
    capture.add_argument("--fp-store", help=argparse.SUPPRESS)
    capture.add_argument("--dump-dir", required=True)
    capture.add_argument("--build-manifest", required=True)
    capture.add_argument("--journal-unit", default="fprintd.service")
    capture.add_argument("operation", nargs=argparse.REMAINDER)

    admit = commands.add_parser("admit")
    admit.add_argument("--corpus", required=True)
    admit.add_argument("--case-id", required=True)
    admit.add_argument("--operation", choices=("enroll", "verify", "identify", "study"),
                       required=True)
    admit.add_argument("--order", type=int, required=True)
    admit.add_argument("--setup", required=True)
    admit.add_argument("--live", action="append", required=True)
    admit.add_argument("--identify-prelude", action="append", default=[],
                       help="Earlier identify frame sharing this preprocessing generation")
    admit.add_argument("--gallery", action="append", default=[])
    admit.add_argument("--gallery-format", choices=("g53m", "raw-packed"), default="g53m")
    admit.add_argument("--image-format", choices=("raw-u16le", "pgm12"), default="raw-u16le")
    admit.add_argument("--coverage", action="append", required=True)
    admit.add_argument("--quick", action="store_true")
    admit.add_argument("--usb-product", required=True)
    admit.add_argument("--chip-id", required=True)
    admit.add_argument("--tcode", type=int, default=121)
    admit.add_argument("--dac-high", type=int, default=125)
    admit.add_argument("--dac-low", type=int, default=198)
    admit.add_argument("--dll-sha256", required=True)
    admit.add_argument("--wine-prefix")

    doctor = commands.add_parser("doctor")
    doctor.add_argument("--corpus", required=True)
    doctor.add_argument("--sealed-prefix")
    doctor.add_argument("--state-root")

    gc = commands.add_parser("gc")
    gc.add_argument("--state-root")
    gc.add_argument("--failures", action="store_true")

    build_manifest = commands.add_parser("build-manifest")
    build_manifest.add_argument("--repo", required=True)
    build_manifest.add_argument("--library", required=True)
    build_manifest.add_argument("--output", required=True)
    build_manifest.add_argument("--debug", action="store_true")

    save_enrollments = commands.add_parser(
        "save-enrollments", help="Make a private copy of the current user's saved fingerprints")
    save_enrollments.add_argument("--output", required=True)
    save_enrollments.add_argument("--user")
    save_enrollments.add_argument("--source", help=argparse.SUPPRESS)

    finish_capture = commands.add_parser(
        "finish-capture", help="Finalize a passive dump and copy the final saved fingerprints")
    finish_capture.add_argument("--dump-dir", required=True)
    finish_capture.add_argument("--user")

    validate_dump = commands.add_parser(
        "validate-dump", help="Discover and validate every usable operation in a dump directory")
    validate_dump.add_argument("--dump-dir", required=True)
    validate_dump.add_argument("--dll", default=os.environ.get("MILAN_PARITY_DLL"))
    validate_dump.add_argument("--approved-dll-sha256",
                               default=os.environ.get("MILAN_PARITY_DLL_SHA256"))
    validate_dump.add_argument("--wine-prefix",
                               default=(os.environ.get("MILAN_PARITY_WINEPREFIX") or
                                        os.environ.get("WINEPREFIX")))
    validate_dump.add_argument("--native-runner",
                               default=str(Path(__file__).resolve().parent / "native-runner"))
    validate_dump.add_argument("--state-root")
    validate_dump.add_argument("--report")

    help_command = commands.add_parser("help", help="Show general or command-specific help")
    help_command.add_argument("topic", nargs="?")
    return parser


def main(argv: Sequence[str] | None = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "help":
            if args.topic:
                parser.parse_args([args.topic, "--help"])
            else:
                parser.print_help()
        elif args.command == "verify":
            run_verify(args)
        elif args.command == "verify-native":
            if args.repetitions < 2:
                raise HarnessError("verify-native requires at least two repetitions")
            run_verify(args, native=True)
        elif args.command == "capture":
            if args.operation and args.operation[0] == "--":
                args.operation = args.operation[1:]
            if not args.operation:
                raise HarnessError("capture requires exactly one operation after --")
            run_capture(args)
        elif args.command == "admit":
            run_admit(args)
        elif args.command == "build-manifest":
            run_build_manifest(args)
        elif args.command == "save-enrollments":
            run_save_enrollments(args)
        elif args.command == "finish-capture":
            run_finish_capture(args)
        elif args.command == "validate-dump":
            run_validate_dump(args)
        elif args.command == "doctor":
            run_doctor(args)
        else:
            run_gc(args)
    except HarnessError as error:
        print(f"milan_parity=fail error={error}", file=sys.stderr)
        raise SystemExit(2) from error


if __name__ == "__main__":
    main()
