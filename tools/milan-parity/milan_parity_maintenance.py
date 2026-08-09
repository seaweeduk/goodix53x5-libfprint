"""Transient parity state maintenance."""

from __future__ import annotations

import argparse
import shutil

from milan_parity_common import HarnessError, locked_state


REPORT_LIMIT = 10


def run_gc(args: argparse.Namespace) -> None:
    removed = []
    with locked_state(args.state_root) as state:
        work = state / "work"
        try:
            if work.exists():
                shutil.rmtree(work)
                removed.append("work")
            reports = state / "reports"
            if reports.exists():
                ordered = sorted((path for path in reports.iterdir()
                                  if path.is_file() and path.suffix == ".json"),
                                 key=lambda path: path.stat().st_mtime, reverse=True)
                for stale in ordered[REPORT_LIMIT:]:
                    stale.unlink()
                    removed.append(str(stale.relative_to(state)))
        except OSError as error:
            raise HarnessError(f"cannot collect transient parity state: {error}") from error
    print(f"milan_parity_gc=pass removed={len(removed)} state={state}")
