# Milan Parity Component Inventory

The maintained surface is a strict current-debug dump workflow.

| Component | Current responsibility |
|---|---|
| `milan-parity` | Exposes only `capture`, `build-manifest`, `save-enrollments`, `finish-capture`, `validate-dump`, `gc`, and `help` |
| `milan_parity_capture.py` | Identifies one new operation, snapshots the complete bounded post-operation dump and saved enrollments, verifies the live library, finalizes passive dumps, and writes private artifact inventories |
| `milan_parity_build.py` | Creates `milan-parity-driver-build/v2` evidence containing a random per-compilation build ID, deterministic production source identity, and captured library path/hash/size |
| `milan_parity_validate.py` | Validates runtime-debug v3, print schema 4, attempt chronology, exact artifact/count identities, sealed build evidence, mandatory selectors, approved DLL/Wine inputs, and selected-operation verdicts |
| `milan_parity_replay.py` | Builds only the transient case-shaped runner boundary and projects exact processed, probe, gallery, candidate, queue, and lifecycle results |
| `native-runner` | Enforces approved DLL identity, existing Wine prefix, pinned canonical source/backend provenance, and natural identify/study batch execution |
| `native/native-oracle.c` | Runs the approved DLL at the controlled-zero boundary and emits exact probe, processed, after-match, candidate, queue, and lifecycle evidence |
| `current-runner` | Optionally compares an instrumented debug-mode-1 build of current source against the same selected transient inputs |
| `milan_parity_maintenance.py` | Deletes transient replay work and bounds retained JSON reports only |

Runtime schema `goodix53x5-runtime-debug/v3` and print schema 4 are the sole
accepted runtime input contract. Exact nullable `preprocess_status_i32` is null
only for an unattempted phase and otherwise preserves the exact int32
preprocessing return. Every runtime-referenced binary artifact uses `.bin`;
templates are raw native payloads. Declared artifact, gallery, and probe record
counts are exact.

Repeated enrollment attempts at one stage are legal and are uniquely ordered by
session chronology. Lifecycle completion means phase success. Contract-valid
failed and retry records retain nullable artifacts, do not poison nonselected
operations, and preserve actual captured lifecycle observations for comparison
with actual native and current runner observations. Retry-only selection/replay
is not yet available.

Manifest creation and live capture rehash and verify the installed library.
Validation treats the manifest's captured library path/hash/size as sealed
evidence and does not require that path to remain installed. Runtime build IDs
provide the exact mixed-build check.

The persistent authorities are the original private dump, its
`milan-parity-driver-build/v2` manifest, the explicitly approved DLL and Wine
environment, and the emitted validation report. The case-shaped runner input is
a transient internal adapter, not a maintained data format. In a clean checkout,
run `GOODIX53X5_DEBUG=0 ./scripts/build-local.sh` to create
`.build/libfprint/builddir` before `--compare-current`; the Milan stack builder
does not create it.

Native provenance is acquired once before execution and fully rechecked after
execution. One pinned backend serves the complete batch, and only provenance
mode may build a missing backend. Validation reports are create-only at both
default and explicit paths.
