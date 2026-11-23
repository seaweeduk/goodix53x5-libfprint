#!/usr/bin/env python3
"""Print the AI disclosure for this job's OpenCode runs, from the models they recorded.

Every assistant message in OpenCode's database records its provider, model and variant, so this
lists what actually ran, subagents included, rather than what was configured.
"""

import sqlite3
import subprocess
import sys
from pathlib import Path

QUERY = """SELECT json_extract(data, '$.model.providerID') AS provider,
    json_extract(data, '$.model.id') AS model,
    json_extract(data, '$.model.variant') AS variant
  FROM session_message
  WHERE type = 'assistant' AND json_extract(data, '$.model.id') IS NOT NULL
  GROUP BY provider, model, variant
  ORDER BY min(time_created)"""


def opencode(*args: str) -> str:
    result = subprocess.run(["opencode", *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ""


def disclosure(role: str) -> str:
    database = opencode("debug", "paths", "db") or str(
        Path.home() / ".local/share/opencode/opencode.db"
    )
    db = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    try:
        rows = db.execute(QUERY).fetchall()
    finally:
        db.close()
    version = opencode("--version").removeprefix("opencode").strip().removeprefix("v")

    lines = [
        "> [!IMPORTANT]",
        "> **AI disclosure**",
        ">",
        "> | Model | Variant | Role |",
        "> | :--- | :--- | :--- |",
    ]
    for provider, model, variant in rows:
        shown = f"`{variant}`" if variant and variant != "default" else "Default"
        lines.append(f"> | `{provider}/{model}` | {shown} | {role} |")
    if not rows:
        lines.append("> | Unknown | Unknown | " + role + " |")
    lines += [">", f"> <sub>Agent harness <strong>OpenCode {version or 'Unknown'}</strong></sub>"]
    return "\n".join(lines)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: disclosure.py ROLE", file=sys.stderr)
        return 2
    print(disclosure(sys.argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
