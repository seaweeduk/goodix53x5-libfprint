"""Driver build-manifest creation and validation."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

from milan_parity_common import (
    DRIVER_BUILD_SCHEMA,
    HarnessError,
    POLICY,
    RUNTIME_SCHEMA,
    load_json,
    require_exact_keys,
    require_sha256,
    run,
    sha256_file,
    source_identity,
    write_atomic,
    canonical,
)


BUILD_FIELDS = {
    "anti_fake_mode",
    "boundary_policy",
    "build_id",
    "debug_enabled",
    "library_bytes",
    "library_path",
    "library_sha256",
    "print_schema",
    "profile",
    "runtime_schema",
    "schema",
    "source_commit",
    "source_identity",
    "subtype",
}

BUILD_ID_MARKER = "goodix53x5-build-id-v1:"
SOURCE_ID_MARKER = "goodix53x5-source-id-v1:"


def _regular_file(path: Path, label: str) -> Path:
    try:
        if path.is_symlink() or not path.is_file():
            raise HarnessError(f"{label} must be a regular non-symlink file: {path}")
    except OSError as error:
        raise HarnessError(f"cannot inspect {label} {path}: {error.strerror or error}") from error
    return path.resolve()


def validate_build_manifest(path: Path, *, verify_library: bool = False) -> dict[str, object]:
    manifest = _regular_file(path.expanduser(), "driver build manifest")
    value = require_exact_keys(load_json(manifest, "driver build manifest"), BUILD_FIELDS,
                               "driver build manifest")
    if value["schema"] != DRIVER_BUILD_SCHEMA:
        raise HarnessError(f"unsupported driver build schema: {value['schema']}")
    if value["runtime_schema"] != RUNTIME_SCHEMA:
        raise HarnessError("driver build runtime schema must be goodix53x5-runtime-debug/v3")
    require_sha256(value["build_id"], "driver build build_id")
    require_sha256(value["source_identity"], "driver build source_identity")
    if (not isinstance(value["source_commit"], str) or
            not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", value["source_commit"])):
        raise HarnessError("driver build source_commit must be a lowercase Git object ID")
    require_sha256(value["library_sha256"], "driver build library_sha256")
    if value["debug_enabled"] is not True:
        raise HarnessError("driver build manifest must describe a debug-enabled library")
    manifest_policy = {key: value[key] for key in POLICY}
    if manifest_policy != POLICY:
        raise HarnessError("driver build manifest policy differs from the fixed profile-9 policy")
    if (not isinstance(value["library_path"], str) or not value["library_path"] or
            not Path(value["library_path"]).is_absolute()):
        raise HarnessError("driver build library_path must be an absolute path")
    if (not isinstance(value["library_bytes"], int) or
            isinstance(value["library_bytes"], bool) or value["library_bytes"] < 1):
        raise HarnessError("driver build library_bytes must be a positive integer")
    if verify_library:
        library = _regular_file(Path(value["library_path"]), "installed library")
        try:
            size = library.stat().st_size
        except OSError as error:
            raise HarnessError(f"cannot stat installed library {library}: {error}") from error
        if size != value["library_bytes"] or sha256_file(library) != value["library_sha256"]:
            raise HarnessError("installed library identity differs from the driver build manifest")
        _verify_library_strings(library, str(value["build_id"]),
                                str(value["source_identity"]))
    return value


def _verify_library_strings(library: Path, build_id: str,
                            expected_source_identity: str) -> None:
    actual_build_id, actual_source_identity = _library_identities(library)
    if actual_build_id != build_id:
        raise HarnessError("installed library build ID differs from the driver build manifest")
    if actual_source_identity != expected_source_identity:
        raise HarnessError("installed library source identity differs from the driver build manifest")


def _library_identities(library: Path) -> tuple[str, str]:
    process = run(("strings", str(library)), "inspect installed library",
                  stdout=-1, stderr=-1)
    strings = process.stdout
    if not isinstance(strings, bytes):
        strings = strings.encode("utf-8")
    if RUNTIME_SCHEMA.encode("ascii") not in strings:
        raise HarnessError("installed library does not contain runtime schema v3")
    identities = []
    for marker_text in (BUILD_ID_MARKER, SOURCE_ID_MARKER):
        marker = marker_text.encode("ascii")
        matches = [line[len(marker):] for line in strings.splitlines()
                   if line.startswith(marker)]
        if len(matches) != 1 or not re.fullmatch(rb"[0-9a-f]{64}", matches[0]):
            label = marker_text.rstrip(":")
            raise HarnessError(
                f"installed library does not contain exactly one valid {label} marker")
        identities.append(matches[0].decode("ascii"))
    return identities[0], identities[1]


def run_build_manifest(args: argparse.Namespace) -> None:
    repo = Path(args.repo).expanduser().resolve()
    if not (repo / ".git").exists():
        raise HarnessError(f"build manifest repository is not a Git worktree: {repo}")
    supplied_library = Path(args.library).expanduser()
    library = _regular_file(supplied_library, "installed library")
    deterministic_source_identity = source_identity(repo)
    build_id, library_source_identity = _library_identities(library)
    if library_source_identity != deterministic_source_identity:
        raise HarnessError("installed library source identity differs from the repository")
    commit_process = run(("git", "rev-parse", "HEAD"), "read source commit",
                         cwd=repo, stdout=-1, stderr=-1, text=True)
    commit = commit_process.stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
        raise HarnessError("Git returned an invalid source commit")
    manifest = {
        "anti_fake_mode": POLICY["anti_fake_mode"],
        "boundary_policy": POLICY["boundary_policy"],
        "build_id": build_id,
        "debug_enabled": True,
        "library_bytes": library.stat().st_size,
        "library_path": str(library),
        "library_sha256": sha256_file(library),
        "print_schema": POLICY["print_schema"],
        "profile": POLICY["profile"],
        "runtime_schema": RUNTIME_SCHEMA,
        "schema": DRIVER_BUILD_SCHEMA,
        "source_commit": commit,
        "source_identity": deterministic_source_identity,
        "subtype": POLICY["subtype"],
    }
    supplied_output = Path(args.output).expanduser()
    if supplied_output.exists() or supplied_output.is_symlink():
        raise HarnessError(
            f"build manifest already exists and will not be overwritten: {supplied_output}")
    output = supplied_output.resolve()
    write_atomic(output, canonical(manifest))
    print(f"milan_parity_build_manifest=pass output={output} build_id={build_id}")
