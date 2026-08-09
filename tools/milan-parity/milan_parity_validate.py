"""Strict runtime-debug/v3 dump validation and natural parity replay."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import stat
from typing import Any
import uuid

from milan_parity_build import validate_build_manifest
from milan_parity_capture import RUNTIME_FILE_RE, artifact_descriptor
from milan_parity_common import (
    HarnessError,
    POLICY,
    REPORT_SCHEMA,
    RUNTIME_SCHEMA,
    UINT32_MAX,
    UINT64_MAX,
    canonical,
    ensure_private_directory,
    locked_state,
    require_exact_keys,
    require_int32,
    require_sha256,
    require_uint,
    run,
    sha256_bytes,
    sha256_file,
    source_identity,
    write_new_atomic,
)
from milan_parity_replay import (
    create_case,
    current_runner_identity,
    difference,
    execute_current,
    execute_native_batch,
    runner_projection,
)


RUNTIME_FIELDS = {
    "action", "action_epoch_u64", "anti_fake_mode", "artifacts",
    "boundary_policy", "build_id", "cancellation", "capture_session_id",
    "chronology_u64", "coverage_i32", "dac_high_u16", "dac_low_u16",
    "errors", "evaluated_gallery_u32", "gallery", "generation_id_u64",
    "generation_use_index_u64", "invalid_gallery_u32", "lifecycle",
    "operation_id", "partition0_count_u32", "partition1_count_u32",
    "preprocess_status_i32", "print_schema", "probe_record_count_u32",
    "profile_u16", "purpose_u32", "quality_i32", "schema", "score_i32",
    "stage_u32", "status_u32", "study_action_u32", "sensor_subtype_u16",
    "tcode_u16", "valid_gallery_u32", "winner_index_u32",
    "winner_position_u64",
}
ARTIFACTS_FIELDS = {
    "final_candidate", "gallery", "live_raw", "native_probe",
    "processed_image", "setup_tx_on",
}
ARTIFACT_FIELDS = {"basename", "bytes_u64", "encoding", "sha256"}
ARTIFACT_GALLERY_FIELDS = {"after_match", "gallery_position_u64", "input"}
CANCELLATION_FIELDS = {
    "driver_observed", "runtime_checkpoint_u32", "runtime_gallery_position_u64",
    "runtime_observed",
}
ERRORS_FIELDS = {"learning", "runtime"}
ERROR_FIELDS = {"code_i32", "domain_u32", "message"}
LIFECYCLE_FIELDS = {"extraction", "preprocess", "study"}
LIFECYCLE_STAGE_FIELDS = {"attempted", "completed"}
GALLERY_FIELDS = {
    "accepted", "after_match_sha256", "evaluated", "gallery_index_u32",
    "gallery_position_u64", "input_template_sha256",
    "lifecycle_update_feature_mask_u64", "matched_feature_u64",
    "queue_counter_after_study_u32", "queue_counter_before_match_u32",
    "queue_eligible_i32", "queue_occupied_after_match_u64",
    "queue_occupied_after_study_u64", "queue_occupied_before_match_u64",
    "queue_state_after_study_u32", "queue_state_before_match_u32", "score_i32",
    "valid", "validation_error",
}
ARTIFACT_ENCODINGS = {
    "final_candidate": "goodix-milan-native-template",
    "gallery_after_match": "goodix-milan-native-template",
    "gallery_input": "goodix-milan-native-template",
    "live_raw": "raw-u16le-12-108x88",
    "native_probe": "goodix-milan-native-template",
    "processed_image": "raw-u8-108x88",
    "setup_tx_on": "raw-u16le-12-108x88",
}
NATIVE_PROVENANCE_SCHEMA = "milan-parity-native-provenance/v1"
NATIVE_PROVENANCE_FIELDS = {
    "architecture", "authority_commit", "dll_sha256", "execution_mode",
    "native_build_sha256", "native_source_sha256", "policy", "schema",
    "wine_version",
}
NATIVE_EXECUTION_MODE = "natural-identify-study-v1"
NATIVE_AUTHORITY_COMMIT = "be2e3ae78ce5942006155aae9dc97e6462f115a2"


def _regular_user_path(value: str, label: str, *, executable: bool = False) -> Path:
    supplied = Path(value).expanduser()
    try:
        metadata = supplied.lstat()
    except OSError as error:
        raise HarnessError(f"{label} is unavailable: {supplied}: {error}") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise HarnessError(f"{label} must be a regular non-symlink file: {supplied}")
    if executable and not os.access(supplied, os.X_OK):
        raise HarnessError(f"{label} is not executable: {supplied}")
    return supplied.resolve()


def _report_path(value: str) -> Path:
    supplied = Path(value).expanduser()
    try:
        if supplied.exists() or supplied.is_symlink():
            raise HarnessError(f"refusing to overwrite existing report: {supplied}")
    except OSError as error:
        raise HarnessError(f"cannot inspect report path {supplied}: {error}") from error
    return supplied.resolve()


def _uuid(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise HarnessError(f"{label} must be a lowercase version-4 UUID")
    try:
        parsed = uuid.UUID(value)
    except (ValueError, AttributeError) as error:
        raise HarnessError(f"{label} must be a lowercase version-4 UUID") from error
    if str(parsed) != value or parsed.version != 4:
        raise HarnessError(f"{label} must be a lowercase version-4 UUID")
    return value


def parse_selector(value: str) -> tuple[str, str, int]:
    parts = value.split("/")
    if len(parts) != 3 or not all(parts):
        raise argparse.ArgumentTypeError("operation must be SESSION/ACTION/EPOCH")
    try:
        session = _uuid(parts[0], "selector session")
    except HarnessError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if parts[1] not in {"enroll", "identify", "verify"}:
        raise argparse.ArgumentTypeError("selector action must be enroll, identify, or verify")
    if not parts[2].isdigit() or int(parts[2]) > UINT64_MAX:
        raise argparse.ArgumentTypeError("selector epoch must be a uint64")
    return session, parts[1], int(parts[2])


def _bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise HarnessError(f"{label} must be a boolean")
    return value


def _nullable_uint(value: Any, maximum: int, label: str) -> int | None:
    return None if value is None else require_uint(value, maximum, label)


def _nullable_int32(value: Any, label: str) -> int | None:
    return None if value is None else require_int32(value, label)


def _nullable_bool(value: Any, label: str) -> bool | None:
    return None if value is None else _bool(value, label)


def _nullable_sha(value: Any, label: str) -> str | None:
    return None if value is None else require_sha256(value, label)


def _error(value: Any, label: str) -> None:
    if value is None:
        return
    error = require_exact_keys(value, ERROR_FIELDS, label)
    require_int32(error["code_i32"], f"{label}.code_i32")
    require_uint(error["domain_u32"], UINT32_MAX, f"{label}.domain_u32")
    if not isinstance(error["message"], str):
        raise HarnessError(f"{label}.message must be a string")


def _artifact(dump: Path, value: Any, encoding: str, label: str,
              seen: set[str], *, optional: bool = False) -> tuple[Path, dict[str, Any]] | None:
    if value is None:
        if optional:
            return None
        raise HarnessError(f"{label} artifact is required")
    descriptor = require_exact_keys(value, ARTIFACT_FIELDS, label)
    basename = descriptor["basename"]
    if (not isinstance(basename, str) or not basename or basename != Path(basename).name or
            "/" in basename or "\\" in basename or not basename.endswith(".bin")):
        raise HarnessError(f"{label}.basename must name one direct .bin dump member")
    if basename in seen:
        raise HarnessError(f"artifact basename is referenced ambiguously: {basename}")
    seen.add(basename)
    require_uint(descriptor["bytes_u64"], UINT64_MAX, f"{label}.bytes_u64")
    require_sha256(descriptor["sha256"], f"{label}.sha256")
    if descriptor["encoding"] != encoding:
        raise HarnessError(f"{label}.encoding must equal {encoding}")
    expected_size = {
        "raw-u16le-12-108x88": 108 * 88 * 2,
        "raw-u8-108x88": 108 * 88,
    }.get(encoding)
    if expected_size is not None and descriptor["bytes_u64"] != expected_size:
        raise HarnessError(f"{label}.bytes_u64 differs from its fixed encoding")
    path = dump / basename
    try:
        metadata = path.lstat()
    except OSError as error:
        raise HarnessError(f"{label} artifact is unavailable: {path}: {error}") from error
    if (not stat.S_ISREG(metadata.st_mode) or stat.S_ISLNK(metadata.st_mode) or
            metadata.st_size != descriptor["bytes_u64"] or
            sha256_file(path) != descriptor["sha256"]):
        raise HarnessError(f"{label} artifact identity differs: {basename}")
    return path, descriptor


def _artifacts(dump: Path, value: Any, gallery_count: int,
               label: str) -> dict[str, Any]:
    artifacts = require_exact_keys(value, ARTIFACTS_FIELDS, label)
    seen: set[str] = set()
    result = {}
    for role in ("setup_tx_on", "live_raw"):
        result[role] = _artifact(dump, artifacts[role], ARTIFACT_ENCODINGS[role],
                                 f"{label}.{role}", seen)
    for role in ("processed_image", "native_probe"):
        result[role] = _artifact(dump, artifacts[role], ARTIFACT_ENCODINGS[role],
                                 f"{label}.{role}", seen, optional=True)
    result["final_candidate"] = _artifact(
        dump, artifacts["final_candidate"], ARTIFACT_ENCODINGS["final_candidate"],
        f"{label}.final_candidate", seen, optional=True)
    if not isinstance(artifacts["gallery"], list) or len(artifacts["gallery"]) != gallery_count:
        raise HarnessError(f"{label}.gallery count differs from runtime gallery")
    result["gallery"] = []
    for position, raw_row in enumerate(artifacts["gallery"]):
        row = require_exact_keys(raw_row, ARTIFACT_GALLERY_FIELDS,
                                 f"{label}.gallery[{position}]")
        if row["gallery_position_u64"] != position:
            raise HarnessError(f"{label}.gallery[{position}] position differs")
        result["gallery"].append({
            "after_match": _artifact(
                dump, row["after_match"], ARTIFACT_ENCODINGS["gallery_after_match"],
                f"{label}.gallery[{position}].after_match", seen, optional=True),
            "input": _artifact(
                dump, row["input"], ARTIFACT_ENCODINGS["gallery_input"],
                f"{label}.gallery[{position}].input", seen, optional=True),
        })
    return result


def validate_runtime(value: Any, path: Path, match: re.Match[str],
                     build_id: str, dump: Path) -> dict[str, Any]:
    if isinstance(value, dict) and value.get("schema") != RUNTIME_SCHEMA:
        raise HarnessError("recreate dump using the current debug build")
    runtime = require_exact_keys(value, RUNTIME_FIELDS, f"runtime record {path.name}")
    if runtime["schema"] != RUNTIME_SCHEMA:
        raise HarnessError(f"unsupported runtime schema in {path.name}: {runtime['schema']}")
    if runtime["build_id"] != build_id:
        raise HarnessError(f"runtime build ID differs from driver-build.json: {path.name}")
    require_sha256(runtime["build_id"], f"{path.name}.build_id")
    session = _uuid(runtime["capture_session_id"], f"{path.name}.capture_session_id")
    if runtime["action"] not in {"enroll", "identify", "verify"}:
        raise HarnessError(f"runtime action is unsupported: {path.name}")
    for field in ("action_epoch_u64", "chronology_u64", "generation_id_u64",
                  "generation_use_index_u64"):
        require_uint(runtime[field], UINT64_MAX, f"{path.name}.{field}")
        if runtime[field] == 0:
            raise HarnessError(f"{path.name}.{field} must be positive")
    for field in ("evaluated_gallery_u32", "invalid_gallery_u32", "partition0_count_u32",
                   "partition1_count_u32", "probe_record_count_u32", "stage_u32",
                   "status_u32", "valid_gallery_u32"):
        require_uint(runtime[field], UINT32_MAX, f"{path.name}.{field}")
    if runtime["status_u32"] > 4:
        raise HarnessError(f"{path.name}.status_u32 is outside the runtime enum")
    if ((runtime["action"] == "enroll" and runtime["stage_u32"] == 0) or
            (runtime["action"] in {"identify", "verify"} and runtime["stage_u32"] != 0)):
        raise HarnessError(f"{path.name}.stage_u32 differs from the action contract")
    for field in ("dac_high_u16", "dac_low_u16", "purpose_u32", "tcode_u16"):
        require_uint(runtime[field], 0xffff, f"{path.name}.{field}")
    for field in ("coverage_i32", "preprocess_status_i32", "quality_i32",
                  "score_i32"):
        _nullable_int32(runtime[field], f"{path.name}.{field}")
    _nullable_uint(runtime["study_action_u32"], 5,
                   f"{path.name}.study_action_u32")
    _nullable_uint(runtime["winner_index_u32"], UINT32_MAX,
                   f"{path.name}.winner_index_u32")
    _nullable_uint(runtime["winner_position_u64"], UINT64_MAX,
                   f"{path.name}.winner_position_u64")
    filename_identity = (match["action"], int(match["epoch"]), int(match["generation"]),
                         int(match["stage"]), int(match["chronology"]))
    record_identity = (runtime["action"], runtime["action_epoch_u64"],
                       runtime["generation_id_u64"], runtime["stage_u32"],
                       runtime["chronology_u64"])
    if filename_identity != record_identity:
        raise HarnessError(f"runtime filename identity differs: {path.name}")
    operation_id = f"{session}/{runtime['action']}/{runtime['action_epoch_u64']}"
    if runtime["operation_id"] != operation_id:
        raise HarnessError(f"runtime operation_id differs: {path.name}")
    expected_purpose = 1 if runtime["action"] == "enroll" else 0
    if (runtime["anti_fake_mode"] != 1 or
            runtime["boundary_policy"] != "canonical-zero-v1" or
            runtime["print_schema"] != 4 or runtime["profile_u16"] != 9 or
            runtime["sensor_subtype_u16"] != 12 or
            runtime["purpose_u32"] != expected_purpose or
            runtime["tcode_u16"] != 121 or runtime["dac_high_u16"] != 125 or
            runtime["dac_low_u16"] != 198):
        raise HarnessError(f"runtime record differs from the fixed profile-9 policy: {path.name}")

    cancellation = require_exact_keys(runtime["cancellation"], CANCELLATION_FIELDS,
                                      f"{path.name}.cancellation")
    for field in ("driver_observed", "runtime_observed"):
        _bool(cancellation[field], f"{path.name}.cancellation.{field}")
    checkpoint = _nullable_uint(cancellation["runtime_checkpoint_u32"], 7,
                                f"{path.name}.cancellation.runtime_checkpoint_u32")
    cancelled_position = _nullable_uint(
        cancellation["runtime_gallery_position_u64"], UINT64_MAX,
        f"{path.name}.cancellation.runtime_gallery_position_u64")
    if cancellation["runtime_observed"] is not (checkpoint is not None):
        raise HarnessError(f"{path.name}.cancellation checkpoint presence differs")
    if checkpoint == 0:
        raise HarnessError(f"{path.name}.cancellation checkpoint cannot be NONE")
    if (cancelled_position is not None) is not (checkpoint is not None and checkpoint >= 4):
        raise HarnessError(f"{path.name}.cancellation gallery position presence differs")
    errors = require_exact_keys(runtime["errors"], ERRORS_FIELDS, f"{path.name}.errors")
    _error(errors["learning"], f"{path.name}.errors.learning")
    _error(errors["runtime"], f"{path.name}.errors.runtime")
    lifecycle = require_exact_keys(runtime["lifecycle"], LIFECYCLE_FIELDS,
                                   f"{path.name}.lifecycle")
    for stage, raw_stage in lifecycle.items():
        stage_value = require_exact_keys(raw_stage, LIFECYCLE_STAGE_FIELDS,
                                         f"{path.name}.lifecycle.{stage}")
        _bool(stage_value["attempted"], f"{path.name}.lifecycle.{stage}.attempted")
        _bool(stage_value["completed"], f"{path.name}.lifecycle.{stage}.completed")
        if stage_value["completed"] and not stage_value["attempted"]:
            raise HarnessError(f"completed lifecycle stage was not attempted: {path.name}")
    preprocess = lifecycle["preprocess"]
    extraction = lifecycle["extraction"]
    study = lifecycle["study"]
    preprocess_status = runtime["preprocess_status_i32"]
    if preprocess["attempted"] is not (preprocess_status is not None):
        raise HarnessError(f"preprocess status presence differs from lifecycle state: {path.name}")
    if preprocess["completed"] is not (preprocess_status == 0):
        raise HarnessError(f"preprocess status differs from completion state: {path.name}")
    if ((runtime["coverage_i32"] is None) is not
            (runtime["quality_i32"] is None)):
        raise HarnessError(f"preprocess output presence differs: {path.name}")
    preprocess_outputs_present = runtime["coverage_i32"] is not None
    if preprocess_outputs_present is not preprocess["attempted"]:
        raise HarnessError(f"preprocess outputs differ from attempt state: {path.name}")
    if preprocess_status in {0x29AA, 0x29BB} and (
            runtime["coverage_i32"] != 0 or runtime["quality_i32"] != 0):
        raise HarnessError(f"early preprocess retry outputs must be zero: {path.name}")
    if preprocess_status == 0xC351 and (
            runtime["status_u32"] != 2 or extraction["attempted"] or
            study["attempted"] or runtime["gallery"] or
            runtime["probe_record_count_u32"] != 0):
        raise HarnessError(f"classification retry lifecycle differs: {path.name}")
    if extraction["attempted"] and not preprocess["completed"]:
        raise HarnessError(f"extraction preceded completed preprocessing: {path.name}")
    if study["attempted"] and not extraction["completed"]:
        raise HarnessError(f"study preceded completed extraction: {path.name}")
    if ((runtime["study_action_u32"] is not None) is not
            study["attempted"]):
        raise HarnessError(f"study action presence differs from lifecycle state: {path.name}")
    if ((runtime["score_i32"] is not None) is not
            (runtime["evaluated_gallery_u32"] > 0)):
        raise HarnessError(f"score presence differs from evaluated gallery count: {path.name}")
    if ((runtime["winner_index_u32"] is None) is not
            (runtime["winner_position_u64"] is None)):
        raise HarnessError(f"winner index/position presence differs: {path.name}")

    if not isinstance(runtime["gallery"], list):
        raise HarnessError(f"{path.name}.gallery must be an array")
    for position, raw_row in enumerate(runtime["gallery"]):
        row = require_exact_keys(raw_row, GALLERY_FIELDS,
                                 f"{path.name}.gallery[{position}]")
        _bool(row["evaluated"], f"{path.name}.gallery[{position}].evaluated")
        for field in ("accepted", "valid"):
            _nullable_bool(row[field], f"{path.name}.gallery[{position}].{field}")
        require_uint(row["gallery_index_u32"], UINT32_MAX,
                     f"{path.name}.gallery[{position}].gallery_index_u32")
        for field in ("queue_counter_after_study_u32",
                      "queue_counter_before_match_u32", "queue_state_after_study_u32",
                      "queue_state_before_match_u32"):
            _nullable_uint(row[field], UINT32_MAX,
                           f"{path.name}.gallery[{position}].{field}")
        require_uint(row["gallery_position_u64"], UINT64_MAX,
                     f"{path.name}.gallery[{position}].gallery_position_u64")
        for field in ("lifecycle_update_feature_mask_u64", "matched_feature_u64",
                      "queue_occupied_after_match_u64",
                      "queue_occupied_after_study_u64", "queue_occupied_before_match_u64"):
            _nullable_uint(row[field], UINT64_MAX,
                           f"{path.name}.gallery[{position}].{field}")
        for field in ("queue_eligible_i32", "score_i32"):
            _nullable_int32(row[field], f"{path.name}.gallery[{position}].{field}")
        for field in ("after_match_sha256", "input_template_sha256"):
            _nullable_sha(row[field], f"{path.name}.gallery[{position}].{field}")
        _error(row["validation_error"], f"{path.name}.gallery[{position}].validation_error")
        if row["gallery_position_u64"] != position:
            raise HarnessError(f"gallery position differs from array order: {path.name}")
        if row["input_template_sha256"] is None:
            raise HarnessError(f"gallery input identity is unavailable: {path.name}")
        evaluated_fields = (
            "accepted", "lifecycle_update_feature_mask_u64", "matched_feature_u64",
            "queue_eligible_i32", "score_i32",
        )
        if any((row[field] is not None) is not row["evaluated"]
               for field in evaluated_fields):
            raise HarnessError(f"gallery evaluated output presence differs: {path.name}")
        if row["valid"] is None and row["evaluated"]:
            raise HarnessError(f"unobserved gallery row was evaluated: {path.name}")
        if row["valid"] is True and not row["evaluated"]:
            raise HarnessError(f"valid gallery row was not evaluated: {path.name}")
        if (row["validation_error"] is not None) is not (row["valid"] is False):
            raise HarnessError(f"gallery validation error presence differs: {path.name}")
        queue_before_fields = (
            "queue_counter_before_match_u32", "queue_occupied_before_match_u64",
            "queue_state_before_match_u32",
        )
        if any((row[field] is not None) is not row["evaluated"]
               for field in queue_before_fields):
            raise HarnessError(f"gallery pre-match queue presence differs: {path.name}")
        if ((row["queue_occupied_after_match_u64"] is not None) is not
                row["evaluated"]):
            raise HarnessError(f"gallery post-match queue presence differs: {path.name}")
        after_match_present = row["after_match_sha256"] is not None
        if after_match_present is not (row["valid"] is True):
            raise HarnessError(f"gallery after-match identity presence differs: {path.name}")
        after_study_expected = (
            study["attempted"] and runtime["winner_position_u64"] == position)
        after_study_fields = (
            "queue_counter_after_study_u32", "queue_occupied_after_study_u64",
            "queue_state_after_study_u32",
        )
        if any((row[field] is not None) is not after_study_expected
               for field in after_study_fields):
            raise HarnessError(f"gallery post-study queue presence differs: {path.name}")
        if row["evaluated"] and (row["accepted"] is not (row["score_i32"] > 0)):
            raise HarnessError(f"gallery score/acceptance is inconsistent: {path.name}")
    if runtime["evaluated_gallery_u32"] != sum(row["evaluated"] for row in runtime["gallery"]):
        raise HarnessError(f"evaluated gallery count differs: {path.name}")
    valid_count = sum(row["valid"] is True for row in runtime["gallery"])
    invalid_count = sum(row["valid"] is False for row in runtime["gallery"])
    if runtime["valid_gallery_u32"] != valid_count:
        raise HarnessError(f"valid gallery count differs: {path.name}")
    if runtime["invalid_gallery_u32"] != invalid_count:
        raise HarnessError(f"invalid gallery count differs: {path.name}")
    if (runtime["probe_record_count_u32"] !=
            runtime["partition0_count_u32"] + runtime["partition1_count_u32"]):
        raise HarnessError(f"probe partition counts differ from record count: {path.name}")
    if extraction["completed"] is not (runtime["probe_record_count_u32"] > 0):
        raise HarnessError(f"probe count presence differs from extraction state: {path.name}")
    if not extraction["completed"] and runtime["gallery"]:
        raise HarnessError(f"gallery exists without completed extraction: {path.name}")
    if study["attempted"] and runtime["winner_position_u64"] is None:
        raise HarnessError(f"study has no winning gallery row: {path.name}")
    if runtime["winner_position_u64"] is not None:
        winner_position = runtime["winner_position_u64"]
        if winner_position >= len(runtime["gallery"]):
            raise HarnessError(f"winner position is outside the gallery: {path.name}")
        winner = runtime["gallery"][winner_position]
        if (winner["gallery_index_u32"] != runtime["winner_index_u32"] or
                winner["accepted"] is not True):
            raise HarnessError(f"winner identity differs from the gallery: {path.name}")
    if runtime["status_u32"] == 0 and runtime["winner_position_u64"] is None:
        raise HarnessError(f"matching runtime has no winner: {path.name}")

    resolved = _artifacts(dump, runtime["artifacts"], len(runtime["gallery"]),
                          f"{path.name}.artifacts")
    processed_present = resolved["processed_image"] is not None
    processed_expected = preprocess["completed"] or preprocess_status == 0xC351
    if processed_present is not processed_expected:
        raise HarnessError(
            f"processed_image artifact/hash presence differs from runtime state: {path.name}")
    if ((resolved["native_probe"] is not None) is not extraction["completed"]):
        raise HarnessError(
            f"native_probe artifact/hash presence differs from runtime state: {path.name}")
    candidate_expected = (
        study["completed"] and
        runtime["study_action_u32"] is not None and
        runtime["study_action_u32"] > 0 and
        errors["learning"] is None and
        not cancellation["runtime_observed"]
    )
    if (resolved["final_candidate"] is not None) is not candidate_expected:
        raise HarnessError(
            f"final_candidate artifact/hash presence differs from runtime state: {path.name}")
    for position, row in enumerate(runtime["gallery"]):
        artifact_row = resolved["gallery"][position]
        for artifact_role, hash_field in (("input", "input_template_sha256"),
                                          ("after_match", "after_match_sha256")):
            descriptor = artifact_row[artifact_role]
            if (descriptor is None) != (row[hash_field] is None):
                raise HarnessError(f"gallery artifact/hash presence differs: {path.name}")
            if descriptor and descriptor[1]["sha256"] != row[hash_field]:
                raise HarnessError(f"gallery artifact/hash identity differs: {path.name}")
        if artifact_row["input"] is None:
            raise HarnessError(f"gallery input artifact is unavailable: {path.name}")
    runtime["_resolved_artifacts"] = resolved
    return runtime


def _crc32_mpeg2(data: bytes) -> int:
    value = 0xffffffff
    for byte in data:
        value ^= byte << 24
        for _ in range(8):
            value = ((value << 1) ^ (0x04c11db7 if value & 0x80000000 else 0)) & UINT32_MAX
    return value


def _verify_runtime_crc(path: Path, expected: str) -> None:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise HarnessError(f"cannot read runtime record {path}: {error}") from error
    if _crc32_mpeg2(data) != int(expected, 16):
        raise HarnessError(f"runtime record CRC differs: {path.name}")


def _load_runtime(path: Path) -> dict[str, Any]:
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


def _reject_noncurrent_runtime(dump: Path) -> None:
    try:
        paths = sorted(dump.iterdir())
    except OSError as error:
        raise HarnessError(
            f"cannot inspect debug dump {dump}: {error.strerror or error}") from error
    for path in paths:
        if path.is_file() and path.name.startswith("runtime-") and path.suffix == ".json":
            try:
                value = json.loads(path.read_bytes())
            except (OSError, json.JSONDecodeError):
                continue
            if isinstance(value, dict) and value.get("schema") != RUNTIME_SCHEMA:
                raise HarnessError("recreate dump using the current debug build")


def _inventory(dump: Path) -> list[dict[str, Any]]:
    descriptors = []
    try:
        for path in sorted(dump.rglob("*")):
            metadata = path.lstat()
            if stat.S_ISLNK(metadata.st_mode):
                raise HarnessError(f"dump must not contain symlinks: {path}")
            if stat.S_ISREG(metadata.st_mode):
                descriptors.append(artifact_descriptor(dump, path))
    except OSError as error:
        raise HarnessError(f"cannot inventory debug dump {dump}: {error}") from error
    return descriptors


def _chronology(runtimes: list[dict[str, Any]]) -> None:
    by_session: dict[str, list[dict[str, Any]]] = {}
    for item in runtimes:
        runtime = item["runtime"]
        by_session.setdefault(runtime["capture_session_id"], []).append(item)
    for session, items in by_session.items():
        items.sort(key=lambda item: item["runtime"]["chronology_u64"])
        chronology = [item["runtime"]["chronology_u64"] for item in items]
        if chronology != list(range(1, len(items) + 1)):
            raise HarnessError(f"runtime chronology is not exact within session {session}")
        last_epoch: dict[str, int] = {}
        generation_setups: dict[int, str] = {}
        uses: dict[tuple[int, str], int] = {}
        for item in items:
            runtime = item["runtime"]
            action = runtime["action"]
            if runtime["action_epoch_u64"] < last_epoch.get(action, 0):
                raise HarnessError(f"runtime action epochs regress within session {session}")
            last_epoch[action] = runtime["action_epoch_u64"]
            setup_sha = runtime["artifacts"]["setup_tx_on"]["sha256"]
            generation = runtime["generation_id_u64"]
            prior_setup_sha = generation_setups.setdefault(generation, setup_sha)
            if prior_setup_sha != setup_sha:
                raise HarnessError(
                    f"generation_id_u64 {generation} has conflicting setup_tx_on "
                    f"SHA-256 values in capture session {session}")
            key = (runtime["generation_id_u64"], setup_sha)
            expected_use = uses.get(key, 0) + 1
            if runtime["generation_use_index_u64"] != expected_use:
                raise HarnessError(f"generation use indexes are not exact in session {session}")
            uses[key] = expected_use


def _preprocess_state_committed(runtime: dict[str, Any]) -> bool:
    return (not runtime["cancellation"]["runtime_observed"] and
            runtime["preprocess_status_i32"] in {0, 0x29AA, 0x7531, 0xC351})


def _prelude(runtimes: list[dict[str, Any]], current: dict[str, Any]) -> list[tuple[Path, int]]:
    runtime = current["runtime"]
    remaining = runtime["generation_use_index_u64"] - 1
    if remaining == 0:
        return []
    key = (runtime["capture_session_id"], runtime["generation_id_u64"],
           runtime["artifacts"]["setup_tx_on"]["sha256"])
    result = []
    earlier = [item for item in runtimes
               if item["runtime"]["capture_session_id"] == runtime["capture_session_id"] and
               item["runtime"]["chronology_u64"] < runtime["chronology_u64"]]
    earlier.sort(key=lambda item: item["runtime"]["chronology_u64"], reverse=True)
    for prior in earlier:
        value = prior["runtime"]
        prior_key = (value["capture_session_id"], value["generation_id_u64"],
                     value["artifacts"]["setup_tx_on"]["sha256"])
        if prior_key != key:
            continue
        if value["generation_use_index_u64"] != remaining:
            raise HarnessError("selected operation has incomplete generation replay chronology")
        if _preprocess_state_committed(value):
            live_raw = value["_resolved_artifacts"]["live_raw"]
            if live_raw is None:
                raise HarnessError(
                    "earlier committed preprocessing frame lacks its live raw artifact")
            result.append((live_raw[0], value["purpose_u32"]))
        remaining -= 1
        if remaining == 0:
            return list(reversed(result))
    raise HarnessError("selected operation has incomplete generation replay chronology")


def _capture_projection(runtime: dict[str, Any]) -> dict[str, Any]:
    artifacts = runtime["_resolved_artifacts"]
    return {
        "acceptance": runtime["status_u32"] == 0,
        "candidate_sha256": (artifacts["final_candidate"][1]["sha256"]
                             if artifacts["final_candidate"] else None),
        "coverage_i32": runtime["coverage_i32"],
        "gallery": [{
            "acceptance": row["accepted"],
            "after_match_sha256": row["after_match_sha256"],
            "gallery_index_u32": row["gallery_index_u32"],
            "gallery_position_u64": row["gallery_position_u64"],
            "queue_occupied_after_match_u64": row["queue_occupied_after_match_u64"],
            "queue_occupied_after_study_u64": row["queue_occupied_after_study_u64"],
            "queue_occupied_before_match_u64": row["queue_occupied_before_match_u64"],
            "score_i32": row["score_i32"],
        } for row in runtime["gallery"]],
        "lifecycle": runtime["lifecycle"],
        "preprocess_status_i32": runtime["preprocess_status_i32"],
        "probe": {
            "active_count_u32": runtime["probe_record_count_u32"],
            "partition0_count_u32": runtime["partition0_count_u32"],
            "partition1_count_u32": runtime["partition1_count_u32"],
            "record_count_u32": runtime["probe_record_count_u32"],
            "sha256": artifacts["native_probe"][1]["sha256"],
        },
        "processed_image_sha256": artifacts["processed_image"][1]["sha256"],
        "quality_i32": runtime["quality_i32"],
        "score_i32": runtime["score_i32"],
        "status_u32": runtime["status_u32"],
        "study_action_u32": runtime["study_action_u32"],
        "winner_index_u32": (runtime["winner_index_u32"]
                             if runtime["winner_index_u32"] is not None else UINT32_MAX),
        "winner_position_u64": (runtime["winner_position_u64"]
                                if runtime["winner_position_u64"] is not None else UINT32_MAX),
    }


def _native_identity(runner: Path, dll: Path, expected_dll_sha256: str, *,
                     build_missing_backend: bool) -> dict[str, Any]:
    if runner.is_symlink() or not runner.is_file() or not os.access(runner, os.X_OK):
        raise HarnessError(f"native runner is absent or not executable: {runner}")
    command = [str(runner), "--provenance", "--dll", str(dll)]
    if not build_missing_backend:
        command.append("--require-existing-backend")
    process = run(command,
                  "native runner provenance", stdout=-1, stderr=-1)
    try:
        provenance = json.loads(process.stdout)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise HarnessError(f"native runner provenance is invalid JSON: {error}") from error
    if process.stdout != canonical(provenance):
        raise HarnessError("native runner provenance is not canonical JSON")
    provenance = require_exact_keys(
        provenance, NATIVE_PROVENANCE_FIELDS, "native runner provenance")
    if provenance["schema"] != NATIVE_PROVENANCE_SCHEMA:
        raise HarnessError("native runner provenance schema is unsupported")
    if provenance["execution_mode"] != NATIVE_EXECUTION_MODE:
        raise HarnessError("native runner does not declare natural execution mode")
    provenance_policy = require_exact_keys(
        provenance["policy"], set(POLICY), "native runner provenance policy")
    if any(type(provenance_policy[key]) is not type(expected) or
           provenance_policy[key] != expected
           for key, expected in POLICY.items()):
        raise HarnessError("native runner provenance policy differs")
    if provenance["dll_sha256"] != expected_dll_sha256:
        raise HarnessError("native runner provenance DLL SHA-256 differs")
    require_sha256(provenance["native_source_sha256"],
                   "native runner provenance native_source_sha256")
    require_sha256(provenance["native_build_sha256"],
                   "native runner provenance native_build_sha256")
    if provenance["architecture"] != "win64-x86_64":
        raise HarnessError("native runner provenance architecture differs")
    if provenance["authority_commit"] != NATIVE_AUTHORITY_COMMIT:
        raise HarnessError("native runner provenance authority commit differs")
    if not isinstance(provenance["wine_version"], str) or not provenance["wine_version"]:
        raise HarnessError("native runner provenance wine_version must be non-empty text")
    return {"dll_bytes": dll.stat().st_size, "dll_path": str(dll),
            "dll_sha256": expected_dll_sha256, "provenance": provenance,
            "runner_path": str(runner), "runner_sha256": sha256_file(runner)}


def run_validate_dump(args: argparse.Namespace) -> None:
    dump = ensure_private_directory(Path(args.dump_dir))
    _reject_noncurrent_runtime(dump)
    supplied_manifest = (Path(args.build_manifest).expanduser() if args.build_manifest else
                         dump.parent / "driver-build.json")
    build = validate_build_manifest(supplied_manifest)
    manifest_path = supplied_manifest.resolve()
    selectors = list(dict.fromkeys(args.operation))
    selector_set = set(selectors)
    if not selectors:
        raise HarnessError("validate-dump requires at least one --operation SESSION/ACTION/EPOCH")
    if not args.dll or not args.wine_prefix:
        raise HarnessError("validate-dump requires a native DLL and existing Wine prefix")
    dll = _regular_user_path(args.dll, "native DLL")
    prefix = Path(args.wine_prefix).expanduser().resolve()
    if not prefix.is_dir():
        raise HarnessError(f"Wine prefix is unavailable: {prefix}")
    dll_sha = sha256_file(dll)
    if not args.approved_dll_sha256:
        raise HarnessError("validate-dump requires an explicitly approved DLL SHA-256")
    require_sha256(args.approved_dll_sha256, "approved DLL SHA-256")
    if dll_sha != args.approved_dll_sha256:
        raise HarnessError("native DLL SHA-256 differs from the explicitly approved identity")
    native_runner = _regular_user_path(
        args.native_runner, "native runner", executable=True)
    inventory = _inventory(dump)
    inventory_sha = sha256_bytes(canonical(inventory))

    runtime_inputs = []
    malformed = []
    for path in sorted(dump.iterdir()):
        if not path.is_file() or not path.name.startswith("runtime-") or path.suffix != ".json":
            continue
        value = _load_runtime(path)
        match = RUNTIME_FILE_RE.fullmatch(path.name)
        if not match:
            malformed.append(path.name)
            continue
        _verify_runtime_crc(path, match["crc"])
        runtime_inputs.append((path, match, value))
    if malformed:
        raise HarnessError("malformed runtime dump files: " + ", ".join(malformed))
    if not runtime_inputs:
        raise HarnessError("dump contains no runtime records")
    build_ids = {value.get("build_id") for _, _, value in runtime_inputs}
    if len(build_ids) != 1:
        raise HarnessError("dump contains mixed runtime build IDs")
    if build_ids != {build["build_id"]}:
        raise HarnessError("runtime build ID differs from driver-build.json")
    runtimes = [{"path": path,
                 "runtime": validate_runtime(value, path, match,
                                             str(build["build_id"]), dump)}
                for path, match, value in runtime_inputs]
    _chronology(runtimes)
    runtimes.sort(key=lambda item: (item["runtime"]["capture_session_id"],
                                    item["runtime"]["chronology_u64"]))
    available = {(item["runtime"]["capture_session_id"], item["runtime"]["action"],
                  item["runtime"]["action_epoch_u64"]) for item in runtimes}
    missing = selector_set - available
    if missing:
        text = ", ".join(f"{session}/{action}/{epoch}"
                         for session, action, epoch in sorted(missing))
        raise HarnessError(f"selected operations are missing: {text}")
    selected_set = selector_set

    current_identity = None
    repo = Path(args.repo).expanduser().resolve()
    if args.compare_current:
        current_runner = _regular_user_path(
            args.current_runner, "current runner", executable=True)
        runner_sha = sha256_file(current_runner)
        production_source_identity = source_identity(repo)
        backend_identity = current_runner_identity(current_runner, repo)
        current_identity = {
            "backend_path": backend_identity["backend_path"],
            "backend_sha256": backend_identity["backend_sha256"],
            "debug": backend_identity["debug"],
            "identity_schema": backend_identity["schema"],
            "repo": str(repo),
            "runner_path": str(current_runner),
            "runner_sha256": runner_sha,
            "source_digest": backend_identity["source_digest"],
            "source_identity": production_source_identity,
        }

    operations = []
    pending = []
    with locked_state(args.state_root) as state:
        report_identity = {
            "comparison": "current-vs-native" if args.compare_current else "capture-vs-native",
            "selectors": [{"action": action, "epoch_u64": epoch, "session": session}
                          for session, action, epoch in selectors],
        }
        output = (_report_path(args.report) if args.report else
                  _report_path(str(state / "reports" / f"dump-{inventory_sha[:16]}-"
                                   f"{sha256_bytes(canonical(report_identity))[:12]}.json")))
        native_identity = _native_identity(
            native_runner, dll, dll_sha, build_missing_backend=True)
        for item in runtimes:
            runtime = item["runtime"]
            identity = (runtime["capture_session_id"], runtime["action"],
                        runtime["action_epoch_u64"])
            selected = identity in selected_set
            operation = {
                "capture_artifacts": runtime["artifacts"],
                "capture_record": artifact_descriptor(dump, item["path"]),
                "comparison": {"differences": [], "status": "skipped", "unavailable": []},
                "generation_id_u64": runtime["generation_id_u64"],
                "natural_queue_observations": {
                    "capture": [{
                        "after_match_u64": row["queue_occupied_after_match_u64"],
                        "after_study_u64": row["queue_occupied_after_study_u64"],
                        "before_match_u64": row["queue_occupied_before_match_u64"],
                        "gallery_position_u64": row["gallery_position_u64"],
                    } for row in runtime["gallery"]],
                    "current": None,
                    "native": None,
                },
                "operation": {"action": identity[1], "epoch_u64": identity[2]},
                "selection_status": "selected" if selected else "skipped",
                "session": identity[0],
                "stage_u32": runtime["stage_u32"],
                "structural_replay_admissibility": {
                    "admissible": False, "reasons": [], "status": "pending"},
            }
            operations.append(operation)
            if not selected:
                operation["comparison"]["reason"] = "operation was not selected"
                operation["structural_replay_admissibility"] = {
                    "admissible": None,
                    "reasons": ["operation was not selected"],
                    "status": "skipped",
                }
                continue
            reasons = []
            if runtime["action"] == "enroll":
                reasons.append("enrollment is not replayable by the natural native authority")
            if (runtime["status_u32"] == 4 or
                    runtime["cancellation"]["runtime_observed"] or
                    runtime["cancellation"]["driver_observed"]):
                reasons.append("cancelled operation is not replayable")
            if not runtime["gallery"]:
                reasons.append("operation has no gallery")
            if any(row["valid"] is not True or not row["evaluated"]
                   for row in runtime["gallery"]):
                reasons.append("gallery contains an invalid or unevaluated row")
            required_artifacts = [runtime["_resolved_artifacts"]["setup_tx_on"],
                                  runtime["_resolved_artifacts"]["live_raw"],
                                  runtime["_resolved_artifacts"]["processed_image"],
                                  runtime["_resolved_artifacts"]["native_probe"]]
            if any(artifact is None for artifact in required_artifacts):
                reasons.append("required replay artifact is unavailable")
            if any(row["input"] is None or row["after_match"] is None
                   for row in runtime["_resolved_artifacts"]["gallery"]):
                reasons.append("gallery replay artifacts are unavailable")
            if reasons:
                operation["structural_replay_admissibility"].update({
                    "reasons": reasons, "status": "fail"})
                operation["comparison"].update({"status": "fail", "unavailable": reasons})
                continue
            try:
                artifacts = runtime["_resolved_artifacts"]
                gallery_inputs = [row["input"][0] for row in artifacts["gallery"]]
                prelude = _prelude(runtimes, item)
                case = create_case(state, dump, item["path"], runtime,
                                   artifacts["setup_tx_on"][0], artifacts["live_raw"][0],
                                   prelude, gallery_inputs, dll_sha, prefix)
                operation["structural_replay_admissibility"].update({
                    "admissible": True, "status": "pass"})
                pending.append({"case": case, "expected": _capture_projection(runtime),
                                "operation": operation})
            except HarnessError as error:
                reason = str(error)
                operation["structural_replay_admissibility"].update({
                    "reasons": [reason], "status": "fail"})
                operation["comparison"].update({"status": "fail", "unavailable": [reason]})

        if pending:
            try:
                native_records = execute_native_batch(native_runner, dll, state,
                                                       [job["case"] for job in pending],
                                                       native_identity["provenance"])
            except HarnessError as error:
                reason = f"native comparison unavailable: {error}"
                for job in pending:
                    job["operation"]["comparison"].update({
                        "status": "fail", "unavailable": [reason]})
                native_records = []
            for job, native_record in zip(pending, native_records):
                try:
                    native_projection, native_unavailable = runner_projection(native_record)
                    if args.compare_current:
                        current_record = execute_current(
                            current_runner, repo, job["case"], backend_identity)
                        expected, current_unavailable = runner_projection(current_record)
                        unavailable = sorted(set(native_unavailable + current_unavailable))
                    else:
                        expected = job["expected"]
                        unavailable = native_unavailable
                    differences = difference(expected, native_projection)
                    job["operation"]["comparison"] = {
                        "differences": differences,
                        "expected_projection": expected,
                        "native_projection": native_projection,
                        "status": "fail" if differences or unavailable else "pass",
                        "unavailable": unavailable,
                    }
                    observations = job["operation"]["natural_queue_observations"]
                    observations["native"] = [{
                        "after_match_u64": row["queue_occupied_after_match_u64"],
                        "after_study_u64": row["queue_occupied_after_study_u64"],
                        "before_match_u64": row["queue_occupied_before_match_u64"],
                        "gallery_position_u64": row["gallery_position_u64"],
                    } for row in native_projection["gallery"]]
                    if args.compare_current:
                        observations["current"] = [{
                            "after_match_u64": row["queue_occupied_after_match_u64"],
                            "after_study_u64": row["queue_occupied_after_study_u64"],
                            "before_match_u64": row["queue_occupied_before_match_u64"],
                            "gallery_position_u64": row["gallery_position_u64"],
                        } for row in expected["gallery"]]
                except HarnessError as error:
                    reason = f"comparison unavailable: {error}"
                    job["operation"]["comparison"].update({
                        "status": "fail", "unavailable": [reason]})

        native_identity_drift = []
        try:
            if _native_identity(native_runner, dll, dll_sha,
                                build_missing_backend=False) != native_identity:
                native_identity_drift.append("native identity changed after execution")
        except (HarnessError, OSError) as error:
            native_identity_drift.append(
                f"native identity recheck failed after execution: {error}")
        if native_identity_drift:
            reasons = [f"native comparison unavailable: {reason}"
                       for reason in native_identity_drift]
            for operation in operations:
                if operation["selection_status"] != "selected":
                    continue
                unavailable = operation["comparison"].get("unavailable", [])
                operation["comparison"].update({
                    "status": "fail",
                    "unavailable": sorted(set(unavailable + reasons)),
                })

        if args.compare_current:
            identity_drift = []
            try:
                if sha256_file(current_runner) != runner_sha:
                    identity_drift.append("current runner SHA-256 changed after execution")
            except HarnessError as error:
                identity_drift.append(f"current runner recheck failed after execution: {error}")
            try:
                if source_identity(repo) != production_source_identity:
                    identity_drift.append("current production source identity changed after execution")
            except (HarnessError, OSError, RuntimeError) as error:
                identity_drift.append(
                    f"current production source recheck failed after execution: {error}")
            try:
                if current_runner_identity(current_runner, repo) != backend_identity:
                    identity_drift.append("current backend identity changed after execution")
            except HarnessError as error:
                identity_drift.append(f"current backend identity recheck failed after execution: {error}")
            if identity_drift:
                reasons = [f"current comparison unavailable: {reason}"
                           for reason in identity_drift]
                for operation in operations:
                    if operation["selection_status"] != "selected":
                        continue
                    unavailable = operation["comparison"].get("unavailable", [])
                    operation["comparison"].update({
                        "status": "fail",
                        "unavailable": sorted(set(unavailable + reasons)),
                    })

        selected_operations = [operation for operation in operations
                               if operation["selection_status"] == "selected"]
        failures = sum(operation["comparison"]["status"] == "fail"
                       for operation in selected_operations)
        passes = sum(operation["comparison"]["status"] == "pass"
                     for operation in selected_operations)
        report = {
            "capture_identity": {"build_id": build["build_id"],
                                  "driver_build_manifest_sha256": sha256_file(manifest_path),
                                  "inventory_sha256": inventory_sha,
                                  "library_bytes": build["library_bytes"],
                                  "library_path_at_capture": build["library_path"],
                                  "library_sha256": build["library_sha256"],
                                  "source_identity": build["source_identity"],
                                  "runtime_schema": RUNTIME_SCHEMA},
            "comparison": "current-vs-native" if args.compare_current else "capture-vs-native",
            "current_identity": current_identity,
            "native_identity": native_identity,
            "operations": operations,
            "parity_mode": "natural",
            "policy": POLICY,
            "schema": REPORT_SCHEMA,
            "selectors": [{"action": action, "epoch_u64": epoch, "session": session}
                          for session, action, epoch in selectors],
            "summary": {"failed": failures, "passed": passes,
                        "selected": len(selected_operations),
                        "skipped_nonselected": len(operations) - len(selected_operations)},
        }
        write_new_atomic(output, canonical(report))
    print(f"milan_parity_dump={'fail' if failures else 'pass'} selected={len(selected_operations)} "
          f"passed={passes} failed={failures} report={output}")
    if failures:
        raise SystemExit(1)
