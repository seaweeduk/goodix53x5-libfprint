# Native Dump Comparison Contract

The native runner is an internal boundary used by `validate-dump`; it is not a
public capture format. The orchestrator supplies one transient case-shaped input
for each selected dump operation and removes that work through normal state
maintenance.

## Authority And Environment

The runner receives an explicit DLL and a transient input that pins the same
lowercase DLL SHA-256. The caller must separately approve that digest. An
existing Wine prefix is mandatory and may be supplied by the transient native
metadata, `MILAN_PARITY_WINEPREFIX`, or `WINEPREFIX`. The runner does not search
for a DLL, run `wineboot`, create or clone a prefix, or choose an implicit
default. `MILAN_PARITY_WINE` may select the Wine executable.

Runner provenance must be canonical and declare:

```text
execution_mode=natural-identify-study-v1
print_schema=4
profile=9
subtype=12
anti_fake_mode=1
boundary_policy=canonical-zero-v1
```

The validator requires the exact provenance schema and fields, fixed policy,
approved DLL digest, native source SHA-256, and native backend SHA-256. It obtains
one strict provenance before execution and supplies that identity to the single
native batch. Batch mode verifies the current source, DLL, and cached backend
bytes against the pinned values, uses one backend for the complete batch, and
cannot build a missing backend. Only provenance mode may build the native
backend. After execution, the validator rehashes the wrapper and obtains and
compares the complete provenance again without rebuilding a missing backend.
Drift makes every selected operation
unavailable and failing while retaining the original report evidence.

The native boundary uses the approved DLL's normal preprocessing, extraction,
identify, study, packing, destruction, and persistence order. At the validated
pre-anti-fake boundary it replaces the DLL-owned 2,288-byte matrix allocation
with a DLL-owned 2,289-byte shadow, preserves bytes 0 through 2,287, and sets
source index 2,288 to zero.

## Inputs

Each replayable operation supplies one TX-on setup frame, one live raw frame,
an ordered input gallery, and any required earlier preprocessing frames. Setup
and live images use the fixed 108x88 raw encodings. Every binary artifact uses a
`.bin` filename. Templates are unwrapped raw native payloads; alternate template
containers are rejected. Descriptors pin exact encodings, byte sizes, and
SHA-256 identities, and gallery and probe record counts are checked exactly.

Earlier frames reconstruct preprocessing state when the selected operation is
not the first use of its generation. They run in exact chronological order
after one preprocessor initialization and before the target frame. Their
captured identify/enroll purpose is explicit. They do not perform extraction,
matching, study, or persistence. Missing earlier generation uses make the
selected operation unavailable and therefore fail validation. Every earlier
generation use consumes its exact generation-use index. A use supplies its live
raw frame when preprocessing was attempted, including recognized retry and
cancellation paths with no processed image; a use where preprocessing was not
attempted supplies no frame. An attempted use without live raw evidence makes
the selected operation unavailable.

Gallery inputs contain only the captured native template and gallery index.
Queue state is not supplied as an independent runner input. The DLL observes
the queue encoded in each loaded template and follows its natural identify and
study lifecycle. The runner records queue occupancy before match, after match,
and after study for exact comparison.

Enrollment and cancelled operations are not executable by this natural native
authority. An empty gallery, invalid or unevaluated gallery rows, missing setup,
live, processed, probe, gallery input, or gallery after-match artifacts, and
incomplete preprocessing chronology are also structurally unavailable. Failed,
cancelled, and retry records are nevertheless structurally valid when lifecycle
state and nullable outputs and artifacts agree. Runtime v3 records exact nullable
`preprocess_status_i32`: null means preprocessing was not attempted, while every
attempt preserves the exact int32 return. Nonzero status requires no processed,
probe, gallery, candidate, or study output. Early `0x29aa` and `0x29bb` retries
require zero quality and coverage; stateful `0x7531` retries preserve the DLL's
returned metrics.
Retry-only selection/replay is not implemented yet, so selecting such an
operation remains unavailable and fails. Nonselected operations do not run
structural replay admissibility and are skipped rather than poisoned by a valid
failure record.

## Exact Outputs

Each successful selected operation returns canonical
`milan-parity-record/v1` output. The orchestrator requires and compares:

- exact raw preprocessing return status, preserving distinct `0x29aa`, `0x29bb`,
  `0x7531`, and any other int32 result, plus processed image SHA-256, quality,
  and coverage;
- serialized probe SHA-256 plus exact active, partition 0, partition 1, and
  total record counts;
- runtime status, overall acceptance, score, winner index, and winner position;
- ordered gallery index, position, score, acceptance, and exact after-match
  template SHA-256;
- final candidate SHA-256 and study action;
- naturally observed queue occupancy before match, after match, and after
  study;
- exact preprocess, extraction, and study attempted/completed lifecycle state.

The after-match template and final candidate are byte-identity comparisons, not
semantic template comparisons. A missing field or unavailable projection is a
selected-operation failure. Lifecycle `completed` means that the phase
succeeded. Native and current runners emit their actual attempted/completed
observations; the orchestrator does not synthesize completion from the requested
operation, status, or presence of another result.

## Reports And Privacy

The public report identifies the capture by its random 256-bit compile-time
build ID, deterministic production source identity,
`milan-parity-driver-build/v2` manifest SHA-256, sealed captured-library
path/hash/size, runtime schema, and dump inventory SHA-256. Validation does not
rehash or require the captured absolute library path. It identifies the native
side by approved DLL path, byte size, SHA-256, runner path and SHA-256, and
canonical provenance. With `--compare-current`, it also records the current
production source identity, repository, runner path and SHA-256, internal
identity schema, exact current-runner source digest and debug mode, and absolute
cached backend path and SHA-256. The validator obtains that canonical identity
once before native execution, supplies its expected source and backend digests
to every current replay, and rechecks the wrapper, production source identity,
and backend identity afterward. Any drift makes every selected current
comparison unavailable and failing without dropping the identity evidence from
the report. The diagnostic backend always uses `GOODIX53X5_DEBUG`
instrumentation and reports debug mode `"1"`; the caller environment does not
select its cache or identity.

In a clean checkout, `--compare-current` requires the caller to run
`GOODIX53X5_DEBUG=0 ./scripts/build-local.sh` first. That command creates
`.build/libfprint/builddir`; the Milan stack builder does not create it for the
current runner.

Each operation retains its session/action/epoch identity, stage and generation,
selection status, structural admissibility, unavailable reasons, exact field
differences, artifact descriptors, and captured/native/current natural queue
observations. Standard output contains only the summary and report path;
temporary biometric artifacts and Wine output are not copied to logs.

Native batch execution is all-or-nothing and is not retried or split. If the
oracle's terminal diagnostic identifies an in-range failed manifest line, the
runner appends that active job's transient case ID and gallery index while
preserving the original process status and stderr. Transient case IDs contain
the capture session UUID, action, epoch, and runtime-record digest, providing an
exact `SESSION/ACTION/EPOCH` attribution without changing persistent schemas.

The default report path is identified by dump-inventory, ordered-selector, and
comparison-mode digests. Callers may instead provide an explicit `--report`
path. Both forms are create-only and validation refuses to overwrite an existing
report.
