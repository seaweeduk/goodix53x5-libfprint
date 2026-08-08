#!/usr/bin/env python3
"""Focused Milan runtime capture and natural parity orchestration."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
from typing import Sequence

from milan_parity_build import run_build_manifest
from milan_parity_capture import run_capture, run_finish_capture, run_save_enrollments
from milan_parity_common import HarnessError
from milan_parity_maintenance import run_gc
from milan_parity_validate import parse_selector, run_validate_dump


PUBLIC_COMMANDS = (
    "capture",
    "build-manifest",
    "save-enrollments",
    "finish-capture",
    "validate-dump",
    "gc",
    "help",
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="milan-parity")
    commands = parser.add_subparsers(dest="command", required=True)

    capture = commands.add_parser("capture", help="Capture one live fprintd operation")
    capture.add_argument("--campaign", required=True)
    capture.add_argument("--user", help="Fingerprint owner; defaults to the current user")
    capture.add_argument("--fp-store", help=argparse.SUPPRESS)
    capture.add_argument("--dump-dir", required=True)
    capture.add_argument("--build-manifest", required=True)
    capture.add_argument("--journal-unit", default="fprintd.service")
    capture.add_argument("operation", nargs=argparse.REMAINDER)

    build = commands.add_parser("build-manifest", help="Describe an installed debug library")
    build.add_argument("--repo", required=True)
    build.add_argument("--library", required=True)
    build.add_argument("--output", required=True)
    build.add_argument("--debug", action="store_true", required=True,
                       help="Require and record runtime-debug/v3 instrumentation")

    save = commands.add_parser(
        "save-enrollments", help="Privately copy the current saved fingerprints")
    save.add_argument("--output", required=True)
    save.add_argument("--user")
    save.add_argument("--source", help=argparse.SUPPRESS)

    finish = commands.add_parser(
        "finish-capture", help="Finalize a passive dump and copy final saved fingerprints")
    finish.add_argument("--dump-dir", required=True)
    finish.add_argument("--user")

    validate = commands.add_parser(
        "validate-dump", help="Validate runtime-debug/v3 and run natural native parity")
    validate.add_argument("--dump-dir", required=True)
    validate.add_argument(
        "--build-manifest",
        help="Canonical driver-build.json; defaults to the parent of --dump-dir")
    validate.add_argument("--operation", action="append", required=True, type=parse_selector,
                          metavar="SESSION/ACTION/EPOCH",
                          help="Select one operation; repeat for additional operations")
    validate.add_argument("--dll", default=os.environ.get("MILAN_PARITY_DLL"))
    validate.add_argument("--approved-dll-sha256",
                          default=os.environ.get("MILAN_PARITY_DLL_SHA256"),
                          help="Required exact DLL SHA-256; may use MILAN_PARITY_DLL_SHA256")
    validate.add_argument("--wine-prefix",
                          default=(os.environ.get("MILAN_PARITY_WINEPREFIX") or
                                   os.environ.get("WINEPREFIX")))
    tool = Path(__file__).resolve().parent
    validate.add_argument("--native-runner", default=str(tool / "native-runner"))
    validate.add_argument("--compare-current", action="store_true",
                          help="Compare rebuilt current source with native for selected operations")
    validate.add_argument("--current-runner", default=str(tool / "current-runner"))
    validate.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    validate.add_argument("--state-root")
    validate.add_argument("--report")

    gc = commands.add_parser("gc", help="Remove transient replay work and bound reports")
    gc.add_argument("--state-root")

    help_command = commands.add_parser("help", help="Show general or command-specific help")
    help_command.add_argument("topic", nargs="?", choices=PUBLIC_COMMANDS)
    return parser


def main(argv: Sequence[str] | None = None) -> None:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "help":
            if args.topic:
                try:
                    parser.parse_args([args.topic, "--help"])
                except SystemExit as error:
                    if error.code:
                        raise
            else:
                parser.print_help()
        elif args.command == "capture":
            if args.operation and args.operation[0] == "--":
                args.operation = args.operation[1:]
            if not args.operation:
                raise HarnessError("capture requires one operation after --")
            run_capture(args)
        elif args.command == "build-manifest":
            run_build_manifest(args)
        elif args.command == "save-enrollments":
            run_save_enrollments(args)
        elif args.command == "finish-capture":
            run_finish_capture(args)
        elif args.command == "validate-dump":
            run_validate_dump(args)
        else:
            run_gc(args)
    except (HarnessError, OSError, PermissionError, subprocess.SubprocessError) as error:
        print(f"milan_parity=fail error={error}", file=sys.stderr)
        raise SystemExit(2) from error


if __name__ == "__main__":
    main()
