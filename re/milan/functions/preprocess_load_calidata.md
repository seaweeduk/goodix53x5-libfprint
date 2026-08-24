# preprocess_load_calidata

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `preprocess_load_calidata`,
  `0x180002ed0..0x1800030c6`.
- Sole caller: `_InitPreProcessor_E` (`FUN_18002bfa0`) at `0x18002c4f4`,
  after its separate 16-byte sensor-ID comparison succeeds and before
  `preprocessor_init`.

## Input And Validation

The input is a calibration payload of at least `0x224b0` bytes. The caller's
sensor-ID prefix is outside this payload and is not rechecked here.

Validation is complete before the first state mutation:

1. A null payload or length below `0x224b0` returns `0x81`.
2. `FUN_1800668e0` supplies the current version string. The comparison length
   includes its NUL when the string length is at most 32; longer strings compare
   only 32 bytes. A mismatch against payload `+0x22490` returns `0x80`.
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
| `+0x1848c` | `0xa000` | `DAT_18023faf8` | external retained block |
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

At the profile-9 live preprocessing boundary, only the loaded sample count and
retained calibration plane survive as consumed state. The setup plane is
replaced before live use, and the external retained blocks have no production
read consumers in this binary. The native calibration file nevertheless keeps
the complete validated payload described above.

The workspace survives operation clear within one engine attachment.
`EngineAdapterDetach` reaches `preprocessor_exit`, which clears the complete
`0x3048c` workspace. A later attachment can repopulate it through this loader
when the persisted sensor-ID prefix, version, and both plane checks pass.

## File Ownership And Write Semantics

The local file is `Goodix\goodix_calib.dat` below the process ProgramData
directory. Its exact size is `0x224c0`: a 16-byte sensor key followed by the
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

## Evidence

- Parameter and version checks: `0x180002f19..0x180002f42`.
- Plane checks: `0x180002f6f..0x180002fa4`.
- Row-major plane copies: `0x180002fb6..0x18000300e`.
- Block, count, and scalar stores: `0x180003010..0x18000306d`.
- Caller sensor-ID gate, fallback, setup, and save:
  `FUN_18002bfa0:0x18002c4c4..0x18002c76c`.
- Exact-size reader: `FUN_180034330 -> FUN_18000d060`.
- Direct final-path writer: `FUN_1800345b0 -> FUN_18000cfa0`.

## Confidence

High for the complete validation, mutation order, field aliases, return values,
caller fallback, and profile-9 attachment lifetime. Vendor names for the two
external retained blocks are not recovered.
