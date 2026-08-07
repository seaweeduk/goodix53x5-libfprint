# Milan Parity Harness

This private-data harness compares the current `goodix53x5` production owners
with an immutable canonical-zero generation made from a user-owned corpus. The
repository contains tooling only. It does not contain or discover fingerprint
images, FP3 files, templates, DLLs, Wine prefixes, or native generations.

The historical 798-row campaign is a private maintainer authority, not a
runtime dependency. Any user can capture their own complete inputs, create a
generation deliberately, and use the same Wine-free verifier afterward.

## Authority Model

The Goodix DLL reads feature-mask source index 2,288 after allocating exactly
2,288 payload bytes. Ordinary Wine output therefore contains allocator residue
and cannot be a deterministic whole-byte authority. `canonical-zero-v1`
replaces only that undefined source with zero.

The retained model is `canonical-zero-layered-v1`:

| Phase | Authority |
|---|---|
| Preprocessing | Exact DLL-defined output |
| Enrollment | DLL-defined fields plus the deterministic canonical-zero output from the digest-pinned validated host-native authority |
| Identify and matching | Exact DLL output using the proven 2,289-byte DLL-owned shadow with source index 2,288 set to zero |
| Study and persistence | Exact controlled-zero DLL lifecycle output |

An uncontrolled Wine enrollment is never labeled canonical parity. Enrollment
generation requires a separately pinned authority runner, executes it twice in
fresh processes, and requires byte-identical canonical records.

## Corpus

The corpus root and every harness state root must be owned by the invoking user
and mode `0700`. Every artifact is named explicitly and checked by size and
SHA-256 before a runner sees it. Absolute paths, traversal, symlinks, unknown
case IDs, missing fields, and policy drift fail closed.

```text
corpus/
  corpus.json
  cases/<opaque-case-id>/
    input.json
    images/*.u16le
    prints/*.bin
  generations/
    ACTIVE
    <generation-id>/
      generation.json
      records/<opaque-case-id>.json
```

`corpus.json` is canonical JSON:

```json
{"cases":[{"coverage":["accepted-positive"],"id":"case-001","input":"cases/case-001/input.json"}],"modes":{"full":["case-001"],"quick":["case-001"]},"policy":{"anti_fake_mode":1,"boundary_policy":"canonical-zero-v1","print_schema":3,"profile":9,"subtype":12},"schema":"milan-parity-corpus/v1"}
```

Every case has schema `milan-parity-case/v1`, an opaque ID, operation order,
device identity, fixed policy, and a content-addressed `artifacts` object. The
`replay` object names artifacts; runners never infer files from directories.

Current replay uses the existing production runtime transaction:

```json
"replay":{"dac_high":125,"dac_low":198,"gallery":[{"artifact":"before_template","index":7}],"live":["live_raw"],"purpose":"identify","setup":"setup_raw","tcode":121}
```

Native identify/study replay adds:

```json
"native":{"authority_model":"canonical-zero-layered-v1","dll_sha256":"<sha256>","schema":"milan-parity-native-replay/v1","wine_prefix":"/persistent/private/sealed-prefix"}
```

See `native/CONTRACT.md` for enrollment authority metadata. Canonical JSON uses
sorted keys, no insignificant whitespace, and one trailing newline.

## Create A Generation

Select the DLL explicitly and approve its digest explicitly. The native runner
will not initialize or discover a Wine prefix.

```sh
export MILAN_PARITY_WINEPREFIX=/persistent/private/sealed-prefix
export MILAN_PARITY_ENROLLMENT_AUTHORITY_RUNNER=/path/to/pinned-authority-runner
export MILAN_PARITY_ENROLLMENT_AUTHORITY_REPO=/path/to/be2e3ae7-worktree

./tools/milan-parity/milan-parity verify-native \
  --corpus /persistent/private/corpus \
  --dll /persistent/private/GoodixEngineAdapter.dll \
  --native-runner ./tools/milan-parity/native-runner \
  --approved-dll-sha256 6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4 \
  --write-new-generation --promote
```

Generation writes a new sibling and refuses to overwrite an existing ID. It
records the corpus-input digest, DLL digest, native wrapper digest, reduced
oracle source/build digests, Wine version and architecture, validated authority
commit, policy, and every canonical record identity. Promotion only changes the
small `generations/ACTIVE` pointer.

Without `--write-new-generation`, `verify-native` reruns the selected DLL and
compares it to the active generation. It never updates expectations.

## Routine Verification

No DLL or Wine prefix is needed:

```sh
./tools/milan-parity/milan-parity verify \
  --repo /path/to/goodix53x5-libfprint \
  --corpus /persistent/private/corpus \
  --current-runner ./tools/milan-parity/current-runner \
  --quick

./tools/milan-parity/milan-parity verify \
  --repo /path/to/goodix53x5-libfprint \
  --corpus /persistent/private/corpus \
  --current-runner ./tools/milan-parity/current-runner \
  --full
```

`quick` and `full` are corpus-declared case lists. `full` must contain every
case; `quick` is a behavior-selected subset. The current runner builds into an
owner-only persistent cache keyed by all algorithm source and runner inputs. It
links the shipping preprocessing, extraction, runtime, queue, study, template,
and persistence owners. It neither includes production `.c` files nor adds a
production API. The non-debug driver build is unchanged.

Every mismatch exits nonzero and reports the case, earliest phase, field,
expected value, actual value, and canonical report path. Use
`--all-differences` only for diagnosis. Normalized reports omit timestamps and
timings, so repeated identical runs produce byte-identical reports; timing is
stored in a separate `.run.json` file.

## Capture

See [`DEBUG-CAPTURE-GUIDE.md`](../../DEBUG-CAPTURE-GUIDE.md) for the complete
managed-stack installation, fprintd configuration, smoke capture, fresh
enrollment, privacy, troubleshooting, and release rollback procedure.

One-operation collection writes a `milan-parity-capture/v2` manifest keyed by
capture session, action, and action epoch. Its `generation_ids_u64` field keeps
generation IDs in first-use order, so one enrollment can cross a reference
refresh without being mistaken for multiple operations.

`validate-dump` replays repeated uses of one preprocessing generation
chronologically when all earlier raw frames are present. Earlier frames are
preprocessing-only preludes; matching and study remain scoped to the target
operation and its captured input gallery. Runtime debug v1 remains readable;
v2 requires a capture-session UUID and never joins preludes across sessions.

## Storage

```sh
./tools/milan-parity/milan-parity doctor --corpus /persistent/private/corpus
./tools/milan-parity/milan-parity gc
./tools/milan-parity/milan-parity gc --failures
```

The default state root is `$XDG_STATE_HOME/milan-parity` or
`~/.local/state/milan-parity`; build artifacts use the corresponding persistent
user cache. Neither path may be group/world accessible. `doctor` reports the
corpus, prefix, cache, quick/full peak estimates, retained-success limit, and
available space. `gc` removes partial work, Wine work, stale builds, and bounded
success-report history without accepting a corpus path and therefore cannot
delete a corpus or generation.

Private convenience assets may live under `tools/milan-parity/private/`, which
is ignored by Git. Do not put private material in another tracked location.
