# preprocess_save_calidata

## Identity And Mapping

- DLL: `GoodixEngineAdapter.dll`.
- Export/body: `preprocess_save_calidata`, `0x180002d00..0x180002ec9`.
- Serializes the calibration workspace; paired importer:
  [`preprocess_load_calidata`](preprocess_load_calidata.md).
- Semantic Linux counterpart: `drivers/goodix53x5/device/persistence.c`,
  `goodix_milan_persistence_save`. That function encodes the consumed retained
  subset in the device-keyed Linux format rather than this native file layout.

## Arguments And Result

The first argument is the destination payload; the second points to its unsigned
32-bit capacity and receives the serialized length. Null destination or capacity
below `0x224b0` returns `0x81` without writing the payload or length. The capacity
pointer is a caller precondition: the function dereferences it without a null
check once the destination is nonnull (`0x180002d50..0x180002d61`). Success returns
zero and writes length `0x224b0` at `0x180002e9d`.

## Serialized Fields And Ownership

The payload offsets, source globals and profile-9 plane sizes are the inverse
of the successful-load table in `preprocess_load_calidata.md`. Copy order is:

1. Calibration plane to `+0x08`, then setup plane to `+0x9928`, each
   `rows * columns * 2` bytes (`0x4a40` for profile 9).
2. Workspace sample count to `+0x18488`.
3. Clear all 32 version bytes at `+0x22490`, then copy
   `min(strlen(version), 32)` bytes, excluding the terminating NUL.
4. Auxiliary `0x800` bytes to `+0x13248`, full-frame `0x4a40` bytes to
   `+0x13a48`, existing workspace packet block `0xa000` bytes to `+0x1848c`,
   and external scalar to `+0x2248c`.
5. Compute the two reflected CRC-32 plane checks into `+0x00` and `+0x04`,
   then publish the exact payload length.

The export does not clear the entire destination: gaps outside those field
writes retain the caller's bytes. The adapter save owner `FUN_18002aef0` supplies
a zeroed complete buffer and adds its separate 16-byte sensor-key prefix.
Only the two image planes have CRCs in this native payload; the history packet,
sample count, auxiliary blocks and external scalar have no separate checksum
here. The serializer does not validate their semantic ranges.

No live gain or classifier state is mutated. In particular, this export copies
the existing packet; it does not call `FUN_18004ff40` to resnapshot extraction's
newer history ring. `FUN_1800508a0` creates that packet at classification end,
before extraction append. See `FUN_18004ff40.md` for packing and snapshot timing,
and `FUN_1800501d0.md` for first-live reconstruction and process-global defaults.

Setup-save and later accepted enrollment/template-update saves share the adapter
save owner; direct final-path write failure is not returned through that owner.
The exact file path, sensor prefix, read/write behavior and caller failure
semantics are maintained in `preprocess_load_calidata.md`.

`FUN_18002aef0` returns zero through its transport return and uses a separate
output status: allocation failure is `0x8001`, serializer failure is `0x8002`,
and successful serialization leaves zero even if the file writer fails.
`FUN_180030b40` passes a 16-byte sensor-key argument to that owner and maps either
nonzero transport return or nonzero output status to `0x8000ffff`; otherwise it
returns zero. The caller-owned output-status pointer and sensor-key pointer are
required. The save owner initializes the output status before its defensive
argument checks.

## Adapter Save Gates

- `_InitPreProcessor_E` (`FUN_18002bfa0`) calls `FUN_18002aef0` after either
  setup attempt succeeds. It saves the loaded/default workspace before the
  first live classifier imports its history packet. Both failed setup attempts
  bypass that save and report algorithm error `0x8002`.
- `EngineAdapterCommitEnrollment` (`FUN_180022700`) calls `FUN_180030b40`
  at `0x180022af4` only after the storage-add call returns a nonnegative HRESULT,
  then calls `FUN_180031120`. It does not replace the storage result with the
  calibration-save result.
- `_CheckIdentifyUpdate_Unify` (`FUN_180031510`) reaches `FUN_180030b40` at
  `0x180031c1e` after a successful-match latch, a positive signed update count,
  an allocated candidate, successful record validation, nonnegative
  storage-delete, and nonnegative storage-add.
  Before calibration save it frees/clears the candidate and its length/count/
  identity fields. Its returned HRESULT remains the storage result.

`EngineAdapterDeactivate` (`FUN_180023610`) is the direct caller of
`FUN_180031510`. With valid adapter/context it calls `FUN_18001e230(adapter, 0)`
first and then performs that update check, ignoring both return values and
returning zero. Null adapter/context instead returns `0x80004003`. This is the
deactivation callback's update/save gate, rather than an unconditional save at
operation clear or detach.

`FUN_18002ba60` copies match byte `DAT_18019b9b4` to context `+0x11c` and
enters `templateStudy` only when that byte is exactly one. A zero match byte
ends the update path before candidate publication. A successful match whose
study update is zero or negative also produces no calibration save: the later
`FUN_180031510` publication predicate is signed context `+0xf0 > 0` together
with nonnull candidate `+0xf8`. A successful comparison alone is not the save
trigger. See `FUN_18002ba60.md` for the match/study handoff.

These file writes are separate from classifier packet serialization:

| Event | Calibration-file effect |
| --- | --- |
| Either setup attempt succeeds | Save before live processing, even if the ensuing image is rejected or never produces a template update |
| Both setup attempts fail | No setup-save |
| Entered live classifier completes | Replace the in-workspace packet; no file write merely for reaching this boundary |
| Preprocessing retry, extraction failure, ordinary no-match, or match with nonpositive study update | No additional post-live calibration save from that result |
| Intermediate enrollment sample accepted or retried | No enrollment-commit save from that attempt |
| Enrollment storage-add succeeds | Save the current workspace |
| Positive identify update passes validation and both storage operations | Save the current workspace |
| Operation clear or adapter detach | No unconditional calibration flush |

An unsaved live mutation remains available to the next ordinary sample in the
same attachment. A later eligible save includes the workspace then present,
rather than only mutations associated with the successful image. A marked
setup reload can instead replace unsaved workspace calibration/count state;
the surviving gain and classifier globals have the separate lifetime described
in `FUN_180031d00.md`.

The setup-save passes the same adapter-status output used by initialization to
`FUN_18002aef0`. Allocation or serialization failure can therefore reject the
sample after `preprocessor_init` has succeeded: `FUN_180031d00` does not publish
its local initialized byte or frame length when that status is nonzero. The
global setup-ready flag remains the result of successful initialization in this
case. File-writer failure alone does not take that path because it does not
change the adapter status. This differs from the enrollment/update callers,
which discard the save result after storage publication.

The latter two gates map to successful publication in `device/enroll.c` and
`device/auth.c`, respectively. The Linux preparation owner
`device/base.c:goodix_milan_generation_prepare_setup` performs restore and
process-state transfer; it has no immediate setup-save call.

The native setup-save copies the just-loaded workspace packet even when older
live classifier globals survive outside the workspace. It does not import or
reserialize those globals. The Linux state after process transfer contains both
the loaded extraction persistence snapshot and surviving live component/support
ages and reference. Its normal `goodix_milan_persistence_save` encodes that
snapshot plus the live age/reference fields, after completed classification at
its production save gates. This mixed post-transfer state is distinct from the
native setup-save packet snapshot. The native packed reference also undergoes
no downsample/reconstruction round trip during setup-save.

For the fields represented by the Linux version-2 format, successful native
setup is an identity transformation of a successfully loaded record:
`FUN_180064bb0` changes the setup plane and setup mean, but leaves the loaded
calibration plane, sample count, and packet unchanged. The setup plane and mean
are absent from the compact format. Re-emitting its validated packed bytes
therefore retains that subset without reference reconstruction/resampling or
reading surviving classifier globals.

The missing/invalid-file fallback has a different initial calibration plane:
`preprocess_init_calidata` writes every calibration word as `0x2000`, count zero,
and a zeroed packet. Successful setup leaves that calibration plane intact for
its immediate save. Linux `goodix_milan_preprocess_reset` instead leaves
`calibration_map` zero while initializing only the live gain maps to unity.
The next live zero-count gain initializer clears calibration in either case,
so the distinction is invisible after that initializer but remains part of the
pre-live setup-save subset. Its semantic compact record has zero counters,
planes, ages, and packed reference, with calibration words `0x2000`.

## Current Linux Save Ownership

All paths below are relative to `drivers/goodix53x5/`.

- `milan/preprocess/classification.c:goodix_milan_profile9_build_broken_mask`
  copies the extraction ring into `extraction_persistence` at classifier end,
  before retry admission and extraction append. This snapshot is distinct from
  the subsequently extended `extraction_classification` ring.
- `device/auth.c:goodix_auth_task_done` and
  `device/enroll.c:goodix_enroll_task_done` install valid worker preprocessing
  state into the current generation before result-status handling, subject to
  action/epoch/generation ownership. Retry/no-match is not itself a RAM-state
  rollback. These copies precede the callbacks' cancellation branches as well.
- Authentication captures `pending_persistence_state` only when a match has
  produced `pending_update_data`. `goodix_verify_complete` saves after replacing
  the target print's raw data and before outward result delivery, provided no
  cancellation/removal/fatal error prevents publication. A cleanup-only error
  can preserve an already completed result and its update.
- Enrollment clears prior `pending_persistence_state` on each accepted stage,
  but takes a new snapshot only at the final required stage with valid
  preprocessing state. `goodix_enroll_complete` reads it only after the required
  stage count and transaction publication succeed, saves, then reports the final
  print. Intermediate stages continue to update generation RAM without taking
  a pending save snapshot or writing the calibration file.
- `pending_persistence_state` is an owned full `GoodixMilanPreprocessState`
  copy. Its production data consumer is `goodix_milan_persistence_save`; error,
  cancellation, result-clear and final-completion paths free it.
- Enrollment also tests that pending pointer for nonnull before publishing the
  completed transaction. The current snapshot allocation does not validate
  persistence ranges; range or filesystem failure in the later void save
  function does not revoke an otherwise valid final print. Authentication
  likewise does not turn a calibration-save refusal into authentication failure.
  Persistence-identity validity is tested at save time, after the session's
  action-completion handoff, rather than sealed when the pending state is copied.
- Both Linux saves precede the daemon's actual print-file storage result. Native
  post-live saves follow successful storage publication. Driver calibration
  persistence and daemon print persistence are separate commits.
