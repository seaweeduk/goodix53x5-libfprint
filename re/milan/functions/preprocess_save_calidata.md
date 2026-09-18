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
  `0x180031c1e` after a positive update count, an allocated candidate, successful
  record validation, nonnegative storage-delete, and nonnegative storage-add.
  Before calibration save it frees/clears the candidate and its length/count/
  identity fields. Its returned HRESULT remains the storage result.

`EngineAdapterDeactivate` (`FUN_180023610`) is the direct caller of
`FUN_180031510`. With valid adapter/context it calls `FUN_18001e230(adapter, 0)`
first and then performs that update check, ignoring both return values and
returning zero. Null adapter/context instead returns `0x80004003`. This is the
deactivation callback's update/save gate, rather than an unconditional save at
operation clear or detach.

The latter two gates map to successful publication in `device/enroll.c` and
`device/auth.c`, respectively. The Linux preparation owner
`device/base.c:goodix_milan_generation_prepare_setup` performs restore and
process-state transfer; it has no immediate setup-save call.
