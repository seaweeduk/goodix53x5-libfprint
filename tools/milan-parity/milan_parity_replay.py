"""Ephemeral case adaptation and runner result projection."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys
from typing import Any

from milan_parity_common import (
    CASE_SCHEMA,
    CORPUS_SCHEMA,
    HarnessError,
    RECORD_SCHEMA,
    canonical,
    require_exact_keys,
    require_int32,
    require_sha256,
    run,
    sha256_bytes,
    sha256_file,
    write_atomic,
)


RUNNER_POLICY = {
    "anti_fake_mode": 1,
    "boundary_policy": "canonical-zero-v1",
    "print_schema": 4,
    "profile": 9,
    "subtype": 12,
}
NATIVE_BATCH_SCHEMA = "milan-parity-native-batch/v1"
CURRENT_IDENTITY_SCHEMA = "milan-parity-current-identity/v1"
CURRENT_IDENTITY_FIELDS = {
    "backend_path", "backend_sha256", "debug", "schema", "source_digest",
}


def _admit(root: Path, source: Path) -> dict[str, Any]:
    try:
        if source.is_symlink() or not source.is_file():
            raise HarnessError(f"replay artifact is absent or a symlink: {source}")
        data = source.read_bytes()
        digest = sha256_bytes(data)
        relative = f"objects/sha256/{digest[:2]}/{digest}"
        destination = root / relative
        destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        if destination.exists():
            if destination.is_symlink() or destination.read_bytes() != data:
                raise HarnessError(f"replay object identity collision: {destination}")
        else:
            write_atomic(destination, data)
    except OSError as error:
        raise HarnessError(f"cannot admit replay artifact {source}: {error}") from error
    return {"bytes": len(data), "path": relative, "sha256": digest}


def create_case(state: Path, dump: Path, runtime_path: Path, runtime: dict[str, Any],
                setup: Path, live: Path, prelude: list[tuple[Path, int]],
                gallery_inputs: list[Path], dll_sha256: str,
                wine_prefix: Path) -> dict[str, Any]:
    operation = (runtime["capture_session_id"], runtime["action"],
                 runtime["action_epoch_u64"])
    digest = sha256_file(runtime_path)
    safe_session = operation[0].replace("-", "")
    root = state / "work" / f"dump-{safe_session}-{operation[1]}-{operation[2]}-{digest[:12]}"
    try:
        if root.exists():
            shutil.rmtree(root)
        root.mkdir(mode=0o700, parents=True)
    except OSError as error:
        raise HarnessError(f"cannot create transient replay case: {error}") from error
    artifacts = {"setup": _admit(root, setup), "live": _admit(root, live)}
    prelude_names = []
    prelude_purposes = []
    for position, (path, purpose) in enumerate(prelude):
        name = f"prelude-{position:03d}"
        artifacts[name] = _admit(root, path)
        prelude_names.append(name)
        prelude_purposes.append(purpose)
    gallery = []
    for position, (row, path) in enumerate(zip(runtime["gallery"], gallery_inputs)):
        name = f"gallery-{position:03d}"
        artifacts[name] = _admit(root, path)
        # Queue state is intentionally not supplied. Both authorities observe their
        # natural queue lifecycle from the serialized input template.
        gallery.append({"artifact": name, "index": row["gallery_index_u32"]})
    case_id = (f"dump-{operation[0]}-{operation[1]}-{operation[2]}-"
               f"{digest[:12]}")
    replay = {
        "dac_high": runtime["dac_high_u16"],
        "dac_low": runtime["dac_low_u16"],
        "gallery": gallery,
        "live": ["live"],
        "native": {"authority_model": "canonical-zero-layered-v1",
                   "dll_sha256": dll_sha256,
                   "schema": "milan-parity-native-replay/v1",
                   "wine_prefix": str(wine_prefix)},
        "purpose": "identify",
        "setup": "setup",
        "tcode": runtime["tcode_u16"],
        "prelude": prelude_names,
        "prelude_purposes": prelude_purposes,
    }
    case = {
        "artifacts": artifacts,
        "device": {"chip_id": "dump", "height": 88, "usb_product": "5335", "width": 108},
        "id": case_id,
        "operation": runtime["action"],
        "order": 0,
        "policy": RUNNER_POLICY,
        "replay": replay,
        "schema": CASE_SCHEMA,
    }
    relative = f"cases/{case_id}/input.json"
    write_atomic(root / relative, canonical(case))
    corpus = {"cases": [{"coverage": ["transient-dump-replay"], "id": case_id,
                           "input": relative}],
              "modes": {"full": [case_id], "quick": [case_id]},
              "policy": RUNNER_POLICY, "schema": CORPUS_SCHEMA}
    write_atomic(root / "corpus.json", canonical(corpus))
    return {"case_id": case_id, "root": root}


def _validate_record(value: Any, case_id: str, label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") != RECORD_SCHEMA:
        raise HarnessError(f"{label} record schema is unsupported")
    if value.get("case_id") != case_id or value.get("policy") != RUNNER_POLICY:
        raise HarnessError(f"{label} record identity or runner policy differs")
    if not isinstance(value.get("phases"), list) or not isinstance(value.get("result"), dict):
        raise HarnessError(f"{label} record is incomplete")
    return value


def _load_stdout(process: Any, case_id: str, label: str) -> dict[str, Any]:
    try:
        value = json.loads(process.stdout)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise HarnessError(f"{label} returned invalid JSON: {error}") from error
    if process.stdout != canonical(value):
        raise HarnessError(f"{label} returned non-canonical JSON")
    return _validate_record(value, case_id, label)


def current_runner_identity(runner: Path, repo: Path) -> dict[str, str]:
    process = run((str(runner), "--identity", "--repo", str(repo)),
                  "current runner identity", stdout=-1, stderr=-1)
    try:
        value = json.loads(process.stdout)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise HarnessError(f"current runner identity returned invalid JSON: {error}") from error
    if process.stdout != canonical(value):
        raise HarnessError("current runner identity returned non-canonical JSON")
    identity = require_exact_keys(value, CURRENT_IDENTITY_FIELDS,
                                  "current runner identity")
    if identity["schema"] != CURRENT_IDENTITY_SCHEMA:
        raise HarnessError("current runner identity schema is unsupported")
    require_sha256(identity["source_digest"], "current runner identity source_digest")
    require_sha256(identity["backend_sha256"], "current runner identity backend_sha256")
    if identity["debug"] != "1":
        raise HarnessError("current runner identity debug must be 1")
    if not isinstance(identity["backend_path"], str):
        raise HarnessError("current runner identity backend_path must be a string")
    backend = Path(identity["backend_path"])
    try:
        valid_backend = (backend.is_absolute() and
                         str(backend.resolve()) == identity["backend_path"] and
                         not backend.is_symlink() and backend.is_file() and
                         os.access(backend, os.X_OK))
    except (OSError, RuntimeError, ValueError) as error:
        raise HarnessError(f"cannot validate current runner backend path: {error}") from error
    if not valid_backend:
        raise HarnessError(
            "current runner identity backend_path is not an absolute executable file")
    if sha256_file(backend) != identity["backend_sha256"]:
        raise HarnessError("current runner identity backend bytes differ from backend_sha256")
    return identity


def execute_current(runner: Path, repo: Path, case: dict[str, Any],
                    identity: dict[str, str]) -> dict[str, Any]:
    process = run((str(runner), "--repo", str(repo), "--corpus", str(case["root"]),
                   "--case", case["case_id"],
                   "--expected-source-digest", identity["source_digest"],
                   "--expected-backend-sha256", identity["backend_sha256"]),
                  "current runner",
                  stdout=-1, stderr=-1)
    return _load_stdout(process, case["case_id"], "current runner")


def execute_native_batch(runner: Path, dll: Path, state: Path,
                          jobs: list[dict[str, Any]],
                          provenance: dict[str, Any]) -> list[dict[str, Any]]:
    batch = {"jobs": [{"case": job["case_id"], "corpus": str(job["root"])}
                      for job in jobs], "schema": NATIVE_BATCH_SCHEMA}
    batch_path = state / "work" / f"native-batch-{os.getpid()}.json"
    batch_written = False
    try:
        write_atomic(batch_path, canonical(batch))
        batch_written = True
        process = run((str(runner), "--batch", str(batch_path), "--dll", str(dll),
                       "--expected-native-source-sha256",
                       provenance["native_source_sha256"],
                       "--expected-native-build-sha256",
                       provenance["native_build_sha256"],
                       "--expected-provenance-schema", provenance["schema"],
                       "--expected-execution-mode", provenance["execution_mode"],
                       "--expected-policy-sha256",
                       sha256_bytes(canonical(provenance["policy"])),
                       "--expected-dll-sha256", provenance["dll_sha256"],
                       "--expected-architecture", provenance["architecture"],
                       "--expected-authority-commit", provenance["authority_commit"],
                       "--expected-wine-version", provenance["wine_version"]),
                       "native runner", stdout=-1, stderr=-1)
        try:
            value = json.loads(process.stdout)
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise HarnessError(f"native runner returned invalid JSON: {error}") from error
        if process.stdout != canonical(value):
            raise HarnessError("native runner returned non-canonical JSON")
        require_exact_keys(value, {"records", "schema"}, "native batch result")
        records = value["records"]
        if (value["schema"] != NATIVE_BATCH_SCHEMA or not isinstance(records, list) or
                len(records) != len(jobs)):
            raise HarnessError("native batch result count differs from selected operations")
        return [_validate_record(record, job["case_id"], "native runner")
                for job, record in zip(jobs, records)]
    finally:
        error_in_flight = sys.exc_info()[0] is not None
        if batch_written:
            try:
                batch_path.unlink()
            except FileNotFoundError:
                pass
            except OSError as error:
                if not error_in_flight:
                    raise HarnessError(
                        f"cannot remove transient native batch manifest {batch_path}: {error}") from error


def runner_projection(record: dict[str, Any]) -> tuple[dict[str, Any], list[str]]:
    phases = {phase.get("name"): phase.get("outputs") for phase in record["phases"]
              if isinstance(phase, dict)}
    try:
        preprocess = phases["stage-01-preprocess"]
        extract = phases["stage-01-extract-antifake"]
        study = phases["stage-01-study"]
        result = record["result"]
        lifecycle = require_exact_keys(
            result["lifecycle"], {"extraction", "preprocess", "study"},
            "runner result.lifecycle")
        for stage, raw_stage in lifecycle.items():
            stage_value = require_exact_keys(
                raw_stage, {"attempted", "completed"},
                f"runner result.lifecycle.{stage}")
            if (type(stage_value["attempted"]) is not bool or
                    type(stage_value["completed"]) is not bool):
                raise HarnessError(f"runner result.lifecycle.{stage} must contain booleans")
            if stage_value["completed"] and not stage_value["attempted"]:
                raise HarnessError(f"runner result.lifecycle.{stage} completed without an attempt")
        projection = {
            "acceptance": result["accepted"],
            "candidate_sha256": study["final_candidate_sha256"],
            "coverage_i32": preprocess["coverage_i32"],
            "gallery": [{
                "acceptance": row["accepted"],
                "after_match_sha256": row["after_match_sha256"],
                "gallery_index_u32": row["gallery_index_u32"],
                "gallery_position_u64": row["gallery_position_u64"],
                "queue_occupied_after_match_u64": row["queue_occupied_after_match_u64"],
                "queue_occupied_after_study_u64": row["queue_occupied_after_study_u64"],
                "queue_occupied_before_match_u64": row["queue_occupied_before_match_u64"],
                "score_i32": row["score_i32"],
            } for row in result["gallery"]],
            "lifecycle": lifecycle,
            "preprocess_status_i32": require_int32(
                preprocess["status_i32"], "runner stage-01-preprocess.status_i32"),
            "probe": {
                "active_count_u32": extract["active_record_count_u32"],
                "partition0_count_u32": extract["partition0_count_u32"],
                "partition1_count_u32": extract["partition1_count_u32"],
                "record_count_u32": extract["probe_record_count_u32"],
                "sha256": extract["probe_template_sha256"],
            },
            "processed_image_sha256": preprocess["processed_image_sha256"],
            "quality_i32": preprocess["quality_i32"],
            "score_i32": result["score_i32"],
            "status_u32": result["status_u32"],
            "study_action_u32": study["study_action_u32"],
            "winner_index_u32": result["winner_index_u32"],
            "winner_position_u64": result["winner_position_u32"],
        }
    except (KeyError, TypeError) as error:
        raise HarnessError(f"runner record is missing comparison output: {error}") from error
    return projection, []


def difference(expected: Any, actual: Any, path: str = "$",
               output: list[dict[str, Any]] | None = None) -> list[dict[str, Any]]:
    found = output if output is not None else []
    if type(expected) is not type(actual):
        found.append({"actual": actual, "expected": expected, "field": path, "reason": "type"})
    elif isinstance(expected, dict):
        for key in sorted(expected.keys() | actual.keys()):
            child = f"{path}.{key}"
            if key not in expected:
                found.append({"actual": actual[key], "expected": "<missing>",
                              "field": child, "reason": "unexpected"})
            elif key not in actual:
                found.append({"actual": "<missing>", "expected": expected[key],
                              "field": child, "reason": "missing"})
            else:
                difference(expected[key], actual[key], child, found)
    elif isinstance(expected, list):
        for position in range(min(len(expected), len(actual))):
            difference(expected[position], actual[position], f"{path}[{position}]", found)
        if len(expected) != len(actual):
            found.append({"actual": len(actual), "expected": len(expected),
                          "field": path, "reason": "length"})
    elif expected != actual:
        found.append({"actual": actual, "expected": expected, "field": path, "reason": "value"})
    return found
