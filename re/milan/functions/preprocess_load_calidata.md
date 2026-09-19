# preprocess_load_calidata

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900.
- Export/body: `preprocess_load_calidata`,
  `0x180002ed0..0x1800030c6`.
- Sole caller: `_InitPreProcessor_E` (`FUN_18002bfa0`) at `0x18002c4f4`,
  after its separate 16-byte sensor-ID comparison succeeds and before
  `preprocessor_init`.
- Paired serializer: [`preprocess_save_calidata`](preprocess_save_calidata.md),
  `0x180002d00`.
- Semantic Linux counterpart: `device/persistence.c:goodix_milan_persistence_restore`,
  selected by `device/base.c:goodix_milan_generation_prepare_setup`. These paths
  are relative to `drivers/goodix53x5/`; the Linux encoding stores the consumed
  calibration/history subset rather than the native payload verbatim.

The restore selector runs at completed-sample delivery, before worker input is
copied, whenever setup is uninitialized or a refresh marker is pending. Hardware
base publication alone does not select the file. Within an initialized engine,
an unmarked hardware-reference replacement leaves the consumed setup reference
and workspace unchanged. `FUN_180031d00.md` maps these separate native/Linux
owners, including live process-state transfer and the `FpDevice` finalization
boundary.

The Linux file is
`/var/lib/fprint/goodix53x5-preprocess-<identity-sha256>.bin`, with exact length
70,323 bytes. `goodix_milan_persistence_prepare` derives its identity from the
`goodix53x5-preprocess-v2` domain, little-endian chip ID, subtype and OTP length,
then the verified OTP bytes. This is a sensor-qualified preprocessing file,
not a user print or a hardware-session checkpoint.

The version-2 encoding contains sample count, calibration plane, three
88x104 retained class planes and their count, component/support counts and age
planes, and the signed 44x54 reference. A 64-byte metadata header and SHA-256
digest qualify that subset. It does not encode setup readiness, hardware or
consumed raw references, live gain planes/count/readiness/stability, adaptive
classifier scalars, import latch, extraction hysteresis, or acquisition state.

`goodix_milan_generation_prepare_setup` returns without reading the file when
setup is initialized and neither engine nor hardware refresh is pending. On a
required setup it resets a temporary generation, calls
`goodix_milan_persistence_restore`, then overlays surviving process-owned state
when the old generation has initialized setup or retained process state. The
restore validates the whole file before decoding directly into that caller-owned
reset state. Its fixed-size decode has no subsequent recoverable failure path.
Missing, invalid or unreadable input leaves the destination unchanged, retaining
the caller's reset defaults. Process-state transfer does not replace the loaded
calibration/count or pre-append extraction snapshot with unsaved live values.

## Input And Validation

The input is a calibration payload of at least `0x224b0` bytes. The caller's
sensor-ID prefix is outside this payload and is not rechecked here.

Validation is complete before the first state mutation:

1. A null payload or length below `0x224b0` returns `0x81`.
2. `FUN_1800668e0` supplies the current version string. The comparison length
   is `min(strlen(version), 32)`, excluding the terminating NUL. A mismatch
   against payload `+0x22490` returns `0x80`. The serializer separately clears
   all 32 version bytes before copying that same bounded string length.
3. `FUN_1800035b0` checks the `rows * columns * 2` bytes at payload `+0x08`
   against dword `+0x00`, then the same byte count at `+0x9928` against dword
   `+0x04`. Either mismatch returns `0x80`.

Rows and columns are unsigned profile globals. The nested copy loops use
unsigned row and column counters and compute the element index in 32 bits as
`row * columns + column`. Profile 9 fixes the dimensions at `108x88`, so each
plane has 9,504 unsigned 16-bit elements and no index or byte-count overflow is
reachable.

The version bytes for this binary are the NUL-terminated string
`Preprocess_v_1.01.01`. `FUN_1800035b0` computes each plane check as reflected
CRC-32 with polynomial `0xedb88320`, initial value `0xffffffff`, and no final
XOR.

## Successful Load

On success the function performs these writes in order:

| Payload offset | Size | Destination | Role |
| --- | ---: | --- | --- |
| `+0x08` | `0x4a40` | workspace `DAT_180219670 + 0x04` | retained calibration plane |
| `+0x9928` | `0x4a40` | workspace `DAT_180219670 + 0x9924` | retained setup plane |
| `+0x13248` | `0x0800` | `DAT_180218e70` | auxiliary calibration block |
| `+0x13a48` | `0x4a40` | `DAT_180249b00` | external full-frame block |
| `+0x18488` | `4` | workspace `DAT_180219670 + 0x00` | unsigned sample count |
| `+0x1848c` | `0xa000` | `DAT_18023faf8`, workspace `+0x26488` | retained broken-level packet and trailing workspace state |
| `+0x2248c` | `4` | `DAT_180249af8` | external retained scalar |
| `+0x22490` | `0x20` maximum | none | version string used only for validation |

The two image planes are copied element by element, row-major. The three
blocks use the overlap-safe copy helper `FUN_18008a9c0`. The sample count is
stored after the first four plane/block writes and before the `0xa000` block
and final scalar. The function does not clear unlisted workspace fields and
returns zero after the final scalar store.

## Caller Failure And Lifetime

`FUN_18002bfa0` treats either nonzero loader result as a request for
`preprocess_init_calidata`. That fallback sets the workspace sample count to
zero, fills the calibration plane with Q13 unity, clears the retained setup
plane, `DAT_18023faf8`, and `DAT_180249af8`, and then continues to
`preprocessor_init`. Its complete contract is documented in
`preprocess_init_calidata.md`. The caller has a defensive branch that maps a
nonzero initializer result to adapter status `0x8002`, but the analyzed
initializer body always returns zero.

After either a successful load or successful fallback, `preprocessor_init`
processes the current setup frame. For profile-9 type 12,
`FUN_180064bb0` replaces the retained setup plane with each normalized setup
sample plus `0x1bb7`; it does not replace the loaded sample count or calibration
plane. The successfully initialized payload is saved again by `FUN_18002aef0`
before the setup call returns. Later successful enrollment commit and
identify-template update paths call `FUN_180030b40`, which reaches the same
serializer after live preprocessing has been able to evolve the workspace.

At the profile-9 live preprocessing boundary, the loaded sample count,
calibration plane, and the prefix of the `0xa000` workspace block survive as
consumed state. `DAT_18023faf8` aliases workspace `DAT_180219670 + 0x26488`.
`FUN_1800501d0` reads its signature, version, scalar counts, packed per-pixel
history/component/support ages, and downsampled reference plane on the first
type-12 classifier call. `FUN_18004ff40` updates that packet after classification
for the next save. The defined packet occupies `0x5ce8` bytes; the remaining
`0x4318` bytes of the transported block have no packet read or write. The setup
plane is replaced before live use. No production
read consumer is established for `DAT_180218e70`, `DAT_180249b00`, or the final
scalar `DAT_180249af8`.

The workspace survives operation clear within one engine attachment.
`EngineAdapterDetach` reaches `preprocessor_exit`, which clears the complete
`0x3048c` workspace. A later attachment can repopulate it through this loader
when the persisted sensor-ID prefix, version, and both plane checks pass.

Calibration reload does not reset process-owned gain initialization at
`DAT_1801efbf8`, readiness at `DAT_1801efbfc`, auxiliary sample count at
`DAT_1801efbf4`, the three gain planes, or coarse-reference/stability state.
Neither these fields nor their lifetime is supplied by the persisted workspace
sample count. The default fallback likewise leaves them intact until live
processing applies its own transitions.

In particular, a live call with imported sample count `1..3` leaves gain
initialization nonzero; loading a later count above three does not turn that
state back into an uninitialized entry. A nested post-render update can clear
readiness before restoring its temporary workspace count. A later zero-count
reload retains that ready value even though the next live initializer resets
the auxiliary count and gain planes. See `FUN_1800672e0.md` and
`FUN_18006c510.md` for these distinct mutation owners, and `FUN_18001ed10.md`
for detach's separate workspace and setup-readiness teardown.

## First-Live Semantic Reconstruction

The loader restores workspace bytes, not a running process snapshot. On a fresh
DLL, the first live gain initializer `FUN_1800672e0` consumes the loaded sample
count and calibration plane: count zero clears calibration and auxiliary count
and fills all three gain planes with `0x2000`; counts one through three select
unity application gain; counts above three normalize the retained calibration
plane by its rounded mean when initialization is still zero. A zero rounded
mean instead resets sample count to zero. All returns leave gain initialization
nonzero. Linux maps this stage to
`milan/core.c:profile9_initialize_gain_state`, independently of packet import.

The first entered classifier `FUN_1800508a0` calls `FUN_1800501d0` only while
`DAT_1801dc9a8` is zero. Packet reconstruction imports the three physical ring
planes, component/support ages and counts, and signed downsampled reference.
Adaptive threshold/average remain the fresh initialized-data values 60/60;
extraction hysteresis remains its fresh zero. The completed classifier writes
the next packet and sets the latch before extraction can append a new plane.
Linux reconstructs these semantic fields during restore and takes the next
pre-append snapshot in
`milan/preprocess/classification.c:goodix_milan_profile9_build_broken_mask`;
`milan/match/info.c:goodix_milan_match_update_extraction_classification`
consumes the restored ring afterward.

The native payload also transports fields absent from that Linux encoding:
the old setup plane (replaced by admitted setup), the two auxiliary blocks at
`DAT_180218e70`/`DAT_180249b00`, scalar `DAT_180249af8`, and unused packet tail.
The auxiliary blocks' copy-in/copy-out routines `FUN_180003190` and
`FUN_1800031d0` have no static code callers; their global cross-references are
those helpers plus load/save. The scalar is read only by save and written by
load/default initialization. Packet import ends at `+0x5ce8`, as detailed in
`FUN_1800501d0.md`. The three transported 9,504-byte ring prefixes exceed the
ordinary extraction consumer's 9,152-byte prefix; see that note for geometry.
These transported fields are distinct from omitted *live* process state:
gain maps/readiness/count/stability, classifier adaptive values/import latch,
and extraction hysteresis are not reconstructed from the file.

Two additional extraction globals are not represented in the Linux state:
prior coverage `DAT_1801dc994` and previous merged high class `DAT_1801dc99c`.
They are not calibration-file fields. On the ordinary auxiliary-selector-zero
route, `FUN_180048260` overwrites prior coverage from the current input before
its append decision, and reads previous high only in the excluded selector-two
branch. It then writes the current merged high. Their fresh zero values and
warm retained values therefore do not supply an additional ordinary-path
persistence input; `FUN_180048260.md` owns the selector and consumer contract.

Ordinary exported calibration sample counts saturate at 400 in
`FUN_180067050`; packet producers cap ring count at three, component count/ages
at five, support count/ages at 50, and append only class values zero through two.
These producer bounds differ from loader validation: the native loader trusts
sample count and packet scalar/packed fields after its version/plane checks.
Linux `goodix_milan_state_valid` and `goodix_milan_persistence_save` enforce the
listed producer bounds in their own format. Those independent range checks do
not establish a particular raw-frame chronology for an arbitrarily assembled
combination of fields.

## File Ownership And Write Semantics

The local file path is constructed by `FUN_180005ec0` as
`<system-drive>:\ProgramData\Goodix\goodix_calib.dat`; the helper obtains the
drive character from `FUN_1800060c0`, which returns the first character of
`GetSystemDirectoryW` output or lowercase `c` when that query fails. It formats
the fixed directory rather than reading a process `ProgramData` environment
variable. Its exact serialized size
is `0x224c0`: a 16-byte sensor key followed by the
`0x224b0`-byte payload described above. `FUN_180034330` attempts one exact-size
read using `_wfopen_s(..., L"rb")`; open failure or a short read clears the
entire caller buffer, which naturally produces a sensor-key mismatch and
default initialization.

`FUN_18002aef0` serializes into a zeroed complete buffer and calls
`FUN_1800345b0`. The latter opens the final path directly with
`_wfopen_s(..., L"wb")`, performs one `fwrite`, closes the stream, and only logs
whether the byte count matched. There is no temporary path, rename, file-buffer
flush, or rollback. The writer has no status return to `FUN_18002aef0`, so an
open or short-write failure does not change the adapter result after successful
serialization; a later read treats a missing, short, or invalid file as default
state.

## Instruction Locations

- Parameter and version checks: `0x180002f19..0x180002f42`.
- Plane checks: `0x180002f6f..0x180002fa4`.
- Row-major plane copies: `0x180002fb6..0x18000300e`.
- Block, count, and scalar stores: `0x180003010..0x18000306d`.
- Caller sensor-ID gate, fallback, setup, and save:
  `FUN_18002bfa0:0x18002c4c4..0x18002c76c`.
- Exact-size reader: `FUN_180034330 -> FUN_18000d060`.
- Direct final-path writer: `FUN_1800345b0 -> FUN_18000cfa0`.
