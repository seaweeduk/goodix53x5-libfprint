#!/usr/bin/env python3
"""Write this run's OpenCode usage in the shape the usage tracker's POST /api/ingest accepts.

The runner's database only ever holds this job, so every session in it is exported. Only usage
fields are read (IDs, times, model, token counts); message content never leaves the runner. The
queries match the tracker plugin's (plugin/src/source.ts in opencode-usage-tracker).
"""

import json
import re
import sqlite3
import sys
from pathlib import Path

MACHINE = "github"

SESSION_QUERY = """SELECT id, project_id, parent_id, title, version, time_created, time_updated,
    cost, tokens_input, tokens_output, tokens_reasoning, tokens_cache_read, tokens_cache_write,
    fork_session_id
  FROM session_v2 ORDER BY time_created, id"""

MESSAGE_QUERY = """SELECT id, session_id, type, seq, time_created, time_updated,
    json_extract(data, '$.model.providerID') AS provider_id,
    json_extract(data, '$.model.id') AS model_id,
    json_extract(data, '$.time.created') AS created,
    json_extract(data, '$.time.completed') AS completed,
    coalesce(json_extract(data, '$.tokens.input'), 0) AS tokens_input,
    coalesce(json_extract(data, '$.tokens.output'), 0) AS tokens_output,
    coalesce(json_extract(data, '$.tokens.reasoning'), 0) AS tokens_reasoning,
    coalesce(json_extract(data, '$.tokens.cache.read'), 0) AS tokens_cache_read,
    coalesce(json_extract(data, '$.tokens.cache.write'), 0) AS tokens_cache_write
  FROM session_message
  WHERE session_id = ? AND type IN ('user', 'assistant')
  ORDER BY time_updated, id"""

# OpenCode copies a parent's messages into a fork with IDs of the form
# <fork event message ID>_<parent seq>. Those copies are the parent's usage.
FORK_COPY_ID = re.compile(r"^msg_[0-9A-Za-z]{26}_[0-9]+$")


def export(database: Path, project_name: str | None) -> dict:
    db = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    db.row_factory = sqlite3.Row
    try:
        sessions: list[dict] = []
        messages: list[dict] = []
        for row in db.execute(SESSION_QUERY):
            session = dict(row)
            fork_of = session.pop("fork_session_id")
            for message in map(dict, db.execute(MESSAGE_QUERY, (session["id"],))):
                copied = FORK_COPY_ID.match(message["id"]) and message["id"].endswith(
                    f"_{message['seq']}"
                )
                if not (fork_of and copied):
                    messages.append(message)
            sessions.append(session)

        used = {session["project_id"] for session in sessions}
        # OpenCode's project ID hashes the git remote, so it matches the one on local machines. The
        # runner's path would replace the local one in the tracker, so it isn't sent.
        projects = [
            {"id": row["id"], "name": project_name or row["name"], "worktree": None}
            for row in db.execute("SELECT id, name FROM project")
            if row["id"] in used
        ]
    finally:
        db.close()

    return {"machine_id": MACHINE, "projects": projects, "sessions": sessions, "messages": messages}


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: export_usage.py DATABASE [PROJECT_NAME]", file=sys.stderr)
        return 2
    database = Path(sys.argv[1])
    if not database.exists():
        print(f"No OpenCode database at {database}", file=sys.stderr)
        return 1
    project_name = sys.argv[2] if len(sys.argv) == 3 and sys.argv[2] else None
    json.dump(export(database, project_name), sys.stdout, separators=(",", ":"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
