# Milan Parity Dumps

This tooling captures private Milan diagnostic dumps and compares explicitly
selected operations with the approved Goodix DLL. It is intentionally strict
and dump-oriented. The repository does not contain or discover fingerprint
images, saved prints, DLLs, Wine prefixes, or validation dumps.

The only public commands are:

```text
capture
build-manifest
save-enrollments
finish-capture
validate-dump
gc
help
```

Use `milan-parity help` or `milan-parity help COMMAND` for the exact arguments.

## Current Dump Contract

The checker accepts exactly runtime schema `goodix53x5-runtime-debug/v3` and
print schema 4.

The fixed policy is sensor subtype 12, profile 9, anti-fake mode 1,
`canonical-zero-v1`, print schema 4, tcode 121, DAC high 125, and DAC low 198.
Runtime JSON must be canonical, internally consistent, and named with the exact
action, epoch, generation, stage, chronology, and CRC identities it contains.
Chronology and generation-use indexes must be complete within each capture
session. Each generation ID is permanently bound to exactly one `setup_tx_on`
SHA-256 within its capture session. Enrollment stage numbers identify the stage
being attempted, not a unique attempt: retries and repeated attempts at one
stage are legal, and the session chronology uniquely orders every record.

Every runtime-referenced binary artifact is a `.bin` file. Templates are raw
native payloads; the validator does not unwrap a driver-owned header or accept
an alternate template container. Every referenced artifact is a direct,
regular, non-symlink dump member with an exact encoding, size, and SHA-256.
Gallery lengths, evaluated/valid/invalid totals, probe record partitions, and
other declared artifact and record counts are checked exactly.

`preprocess_status_i32` is exact nullable runtime evidence. It is null only when
preprocessing was not attempted. Every attempted call records its exact int32
return, including zero, `0x29aa`, `0x29bb`, `0x7531`, and unrecognized values.
A zero status requires completed preprocessing and its processed artifact;
nonzero status requires incomplete preprocessing, zero quality and coverage,
and no extraction, gallery, candidate, or study outputs.

## Build Seal

Every debug compilation embeds a unique random 256-bit build ID. It separately
embeds a deterministic production source identity derived from the production
driver and fixed source-identity inputs. Recompiling unchanged source therefore
keeps the source identity but receives a different build ID.

`build-manifest` writes the exact `milan-parity-driver-build/v2` evidence for an
installed debug library: both embedded identities, source commit, runtime
schema, fixed policy, and the library's absolute path, byte size, and SHA-256 at
manifest creation. The command rehashes the installed library and verifies its
runtime schema, build ID, and source identity against the repository. Live
`capture` rehashes the same installed path, verifies both embedded identities,
and confirms that fprintd mapped that library.

`validate-dump` treats the manifest's captured path, hash, and size as sealed
evidence. It does not require that absolute path to exist or still contain the
captured library. A fix, another debug compilation, or a release library may be
installed while the investigation continues. Runtime records carry the random
build ID, so mixed runtime builds or a runtime/manifest build-ID mismatch are
detected exactly without consulting the installed path.

Create a manifest beside a fresh dump directory:

```sh
./tools/milan-parity/milan-parity build-manifest \
  --repo "$PWD" \
  --library /opt/goodix53x5-milan/lib/libfprint-2.so.2.0.0 \
  --output /private/CAMPAIGN/driver-build.json \
  --debug
```

`build-manifest` never overwrites an existing output.

## Capture Commands

`capture` wraps exactly one live fprintd operation. It uses files added during
the command to identify exactly one new session/action/epoch, then snapshots the
complete bounded post-operation dump. Retaining all files present after the
operation preserves earlier session and generation dependencies needed to
replay the new operation. The command also snapshots that user's saved prints
before and after, records the bounded journal range, verifies the loaded
library, and writes `milan-parity-capture/v3` plus checksums into a new
owner-only destination.

```sh
./tools/milan-parity/milan-parity capture \
  --campaign /private/CAMPAIGN/operations/verify-001 \
  --dump-dir /private/CAMPAIGN/debug-dump \
  --build-manifest /private/CAMPAIGN/driver-build.json \
  -- fprintd-verify "$USER"
```

The persistent debug drop-in can instead collect normal fprintd use across
service restarts and reboots. `save-enrollments` makes an owner-only snapshot of
the current user's saved prints. `finish-capture` retires a passive dump by
copying the final saved prints, taking ownership, enforcing private permissions,
and writing the `milan-parity-finished-capture/v2` inventory. See
[`DEBUG-CAPTURE-GUIDE.md`](../../DEBUG-CAPTURE-GUIDE.md) for the restart-safe
installation and retirement sequence.

## Validation

Validation always requires at least one explicit, repeatable selector:

```text
--operation SESSION/ACTION/EPOCH
```

`SESSION` is the runtime record's lowercase version-4 capture UUID, `ACTION` is
`enroll`, `identify`, or `verify`, and `EPOCH` is that action's uint64 epoch.
There is no implicit whole-dump selection.

The DLL path, its explicitly approved SHA-256, and an already initialized Wine
prefix are mandatory. The tooling does not discover or create a DLL or prefix.

```sh
./tools/milan-parity/milan-parity validate-dump \
  --dump-dir /private/CAMPAIGN/debug-dump \
  --operation 01234567-89ab-4cde-8123-456789abcdef/identify/58 \
  --operation 01234567-89ab-4cde-8123-456789abcdef/verify/59 \
  --dll /private/GoodixEngineAdapter.dll \
  --approved-dll-sha256 APPROVED_LOWERCASE_SHA256 \
  --wine-prefix /private/existing-wine-prefix
```

The `MILAN_PARITY_DLL`, `MILAN_PARITY_DLL_SHA256`, and
`MILAN_PARITY_WINEPREFIX` environment variables may supply those three values.
The DLL must exactly match the approved digest, and the native runner provenance
must be canonical `milan-parity-native-provenance/v1`, declare
`natural-identify-study-v1` and the fixed policy, and identify the exact DLL,
native source, and native backend bytes. Validation obtains this provenance once
before execution, pins the complete identity into one native batch, and rechecks
the wrapper and complete provenance afterward. Batch mode never builds or
selects another backend; provenance mode is the only mode that may build a
missing native backend, and the final provenance recheck requires that pinned
backend to remain present. Any native identity drift makes every selected operation
unavailable and failing while preserving the original identity evidence.

To select every identify or verify operation that has a nonempty gallery, build
the repeated explicit selectors from the current runtime records and issue one
validation command:

```sh
REPO=${REPO:-$PWD}
DUMP_DIR=${DUMP_DIR:?set DUMP_DIR}
BUILD_MANIFEST=${BUILD_MANIFEST:?set BUILD_MANIFEST}
DLL=${DLL:?set DLL}
APPROVED_DLL_SHA256=${APPROVED_DLL_SHA256:?set APPROVED_DLL_SHA256}
WINE_PREFIX=${WINE_PREFIX:?set WINE_PREFIX}

mapfile -t operations < <(
  python3 - "$DUMP_DIR" <<'PY'
import json
import pathlib
import sys

dump = pathlib.Path(sys.argv[1])
selected = set()
for path in dump.glob("runtime-*.json"):
    runtime = json.loads(path.read_bytes())
    if (runtime.get("schema") == "goodix53x5-runtime-debug/v3" and
            runtime.get("action") in {"identify", "verify"} and
            runtime.get("gallery")):
        selected.add((runtime["capture_session_id"], runtime["action"],
                      runtime["action_epoch_u64"]))
for session, action, epoch in sorted(selected):
    print(f"{session}/{action}/{epoch}")
PY
)
((${#operations[@]})) || { printf 'no identify/verify operations with galleries\n' >&2; exit 1; }

selectors=()
for operation in "${operations[@]}"; do
  selectors+=(--operation "$operation")
done

"$REPO/tools/milan-parity/milan-parity" validate-dump \
  --dump-dir "$DUMP_DIR" \
  --build-manifest "$BUILD_MANIFEST" \
  "${selectors[@]}" \
  --dll "$DLL" \
  --approved-dll-sha256 "$APPROVED_DLL_SHA256" \
  --wine-prefix "$WINE_PREFIX"
```

The recipe only constructs repeated public `--operation SESSION/ACTION/EPOCH`
arguments. There is no implicit select-all mode or additional public command.

Natural mode loads each captured gallery template and lets matching and study
observe and mutate its serialized queue state. It compares queue occupancy
before match, after match, and after study. Captured queue state is comparison
evidence and is never supplied as independent native-runner input.

For every admissible selected identify or verify operation, parity compares the
full exact projection:

- raw preprocessing return status, including distinct `0x29aa`, `0x29bb`, and
  `0x7531` retry evidence, plus processed image SHA-256, quality, and coverage;
- probe template SHA-256 and exact active/partition record counts;
- runtime status, overall acceptance, score, winner index, and winner position;
- each gallery row's index, position, score, acceptance, and exact after-match
  template SHA-256;
- final learned candidate SHA-256 and study action;
- queue occupancy before match, after match, and after study;
- preprocess, extraction, and study attempted/completed lifecycle state.

`completed` means that the corresponding lifecycle phase succeeded, not merely
that it returned. Failed, cancelled, and retry records remain structurally valid
when their unavailable outputs and artifacts are null as required by the
lifecycle state. Such records do not make nonselected operations fail. A
selected operation still fails if its required replay projection is
unavailable. Retry-only selection/replay is not implemented yet.

The captured runtime, native runner, and current runner report their actual
lifecycle observations. The comparison does not infer lifecycle completion from
the requested operation or from another output field.

Generation replay consumes every earlier same-generation use index in exact
order. It supplies the live raw frame and captured identify/enroll purpose for
every use whose lifecycle says preprocessing was attempted, including recognized
retry and cancellation paths even when no processed image exists. Uses where
preprocessing was not attempted consume their chronology position but supply no
frame. An attempted use without its live raw artifact makes the selected
operation unavailable.

With `--compare-current`, the expected projection comes from the current source
runner instead of the captured runtime, while the same selected inputs and
native comparison are retained. Before native execution, the validator obtains
one canonical `milan-parity-current-identity/v1` response that pins the exact
current-runner source digest, debug mode `"1"`, absolute cached backend path, and
backend SHA-256. Every selected current replay must use those expected source
and backend identities. The validator rechecks the wrapper, production source
identity, and full backend identity after execution; drift makes every selected
current comparison unavailable and failing while preserving the report evidence.
In a clean checkout, first create the support build directory explicitly:

```sh
GOODIX53X5_DEBUG=0 ./scripts/build-local.sh
```

This creates `.build/libfprint/builddir`. The Milan stack builder does not create
that checkout-local directory for `--compare-current`.

## Active-Investigation Guarantee

A v3 dump remains replayable throughout an active investigation while its
runtime, print, and replay contracts remain current. Ordinary production
algorithm changes do not invalidate the recorded raw-frame and gallery boundary,
so a mismatch can be fixed and replayed against the exact operation that exposed
it. Replacing the installed debug library or returning to a release installation
also does not invalidate the manifest's sealed captured-library evidence.

This guarantee does not cover sensor acquisition, USB lifecycle, FDT/reference
capture, suspend, or timing before the recorded raw-frame boundary; an input
gallery already corrupted by an earlier bug; an incompatible print schema;
uncaptured required process or hardware state; a changed native boundary policy;
or a schema that is no longer current. These cases require a fresh capture, not
a compatibility adapter.

Every explicitly selected, structurally replayable identify or verify operation
from a current strict dump is executed against the approved DLL under the
approved Wine environment and receives the full exact comparison above. A
selected operation is never downgraded to a skip: missing artifacts, incomplete
generation chronology, enrollment, cancellation, an empty gallery, an invalid
or unevaluated gallery row, runner failure, or any other unavailable comparison
is a failure. Operations present in the dump but not selected are the only
operations skipped.

Native batches remain all-or-nothing and are never retried or split. When the
native oracle terminates with an in-range `batch replay failed at line N`, the
failure includes the exact transient case ID and gallery index; that case ID
contains the capture session UUID, action, epoch, and runtime-record digest so it
maps directly to `SESSION/ACTION/EPOCH`.

The report preserves all operation identities and distinguishes them with
`selection_status`. It records the capture build ID, driver-build manifest
SHA-256, deterministic production source identity, captured library path, hash,
and size, dump inventory SHA-256, runtime schema, ordered selectors, approved DLL
and native-runner provenance, optional current production/source/backend
identity with wrapper and backend hashes, per-operation structural admissibility,
exact differences, unavailable reasons, and natural queue observations. Its
summary reports `selected`, `passed`, `failed`, and
`skipped_nonselected` counts. Any selected failure exits nonzero.

The default report filename is keyed by the dump inventory, ordered selectors,
and comparison mode. `--report` may select an explicit owner-only path. Default
and explicit report paths are create-only: validation refuses an existing path
and never overwrites a report.

## Internal Boundary

The orchestrator may adapt selected dump artifacts into a transient case-shaped
runner boundary under the private state directory. That representation is an
internal execution detail only. It is not a persistent artifact model and is
deleted by `gc`; users should retain the original dump, build manifest, and
validation report.

## Storage And Privacy

Dump, campaign, and state directories must be owner-only and must not contain
symlinks. Do not publish images, templates, saved fingerprints, template hashes,
journal ranges, DLL output, or reports.

The default state root is `$XDG_STATE_HOME/milan-parity` or
`~/.local/state/milan-parity`. `gc` removes only transient replay work and trims
JSON reports to the bounded retained set. It does not accept a dump path and
does not delete captures, build manifests, or saved enrollments.

```sh
./tools/milan-parity/milan-parity gc
```
