# Native Runner Contract

`native-runner --corpus ROOT --case CASE_ID --dll PATH` reads only the named
corpus, case descriptor, `input.json`, and artifacts named by its replay
metadata. It never searches for a corpus or DLL.

Every case must retain the fixed parity policy and add this replay contract:

```json
"native": {
  "authority_model": "canonical-zero-layered-v1",
  "dll_sha256": "<lowercase SHA-256 of the explicit DLL>",
  "schema": "milan-parity-native-replay/v1",
  "wine_prefix": "/absolute/path/to/an/existing/prefix"
}
```

The prefix may instead be supplied through `MILAN_PARITY_WINEPREFIX` or
`WINEPREFIX`. It must already be initialized and expose the standard `z:` root
mapping. The runner does not run `wineboot`, create, clone, or select a default
prefix. `MILAN_PARITY_WINE` may select the Wine executable.

Identify, verify, study, and persistence cases require one setup artifact, one
live artifact, and one gallery entry. They use the normal DLL preprocessing,
extraction, `identifyImage`, optional `templateStudy`, packing, and destruction
order. At the validated pre-anti-fake boundary, the runner replaces the
DLL-owned 2,288-byte matrix allocation with a DLL-owned 2,289-byte shadow,
preserves bytes 0 through 2,287, and sets source index 2,288 to zero.

Cases whose target frame is not the first use of its preprocessing generation
may name earlier raw-frame artifacts in replay field `prelude`. The runner
processes them in array order after one `preprocessor_init` and before the target
frame, without extracting, matching, or studying them. This reconstructs DLL
preprocessing history while leaving the target gallery and persistence state
explicit. Capture validation derives the ordered prelude only when all preceding
generation uses and their exact raw artifacts are available.

Enrollment is not a controlled-zero DLL authority. Enrollment cases must add:

```json
"enrollment_authority": {
  "model": "defined-byte-deterministic-replacement-v1",
  "runner_sha256": "<lowercase SHA-256>"
}
```

The corresponding executable path is supplied explicitly in
`MILAN_PARITY_ENROLLMENT_AUTHORITY_RUNNER`. That host-native runner must accept
the same command contract and enforce the experiment's DLL-defined projection
and canonical-zero deterministic replacement. It is digest checked, run twice,
and its canonical records must be identical. An ordinary DLL enrollment replay
is rejected rather than mislabeled.

If the authority runner requires a validated source checkout, set
`MILAN_PARITY_ENROLLMENT_AUTHORITY_REPO`; the wrapper passes it as an explicit
`--repo PATH`. The checkout identity remains part of that runner's own seal.

Generation metadata classifies phases as `defined-byte-parity`,
`deterministic-replacement`, or `functional-parity`. Standard output contains
only the comparable canonical `milan-parity-record/v1`; temporary biometric
artifacts and Wine output are never copied to standard output or logs.
