# Profile-9 / Type-12 Outer Template Codec

## Scope And Identity

This contract covers the outer packed template consumed by the profile-9,
sensor-type-12 identify/study path in `GoodixEngineAdapter.dll` 2.0.310.900,
SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
Feature tag-`95` elements are opaque variable-size children here.

The public exports and principal owners are:

| Function | Address/body | Role |
| --- | --- | --- |
| `templateGetPackedSize` | `0x180002160..0x1800021a6` | Public size query |
| `templatePack` | `0x1800020b0..0x18000215c` | Public serializer |
| `templateUnPack` | `0x1800021b0..0x180002284` | Public decoder |
| `templateDelete` | `0x180002290..0x18000230a` | Public owner destructor |
| `FUN_18003f510` | `0x18003f510..0x18003f699` | Internal size calculation |
| `FUN_18003eaf0` | `0x18003eaf0..0x18003efea` | Complete encoder |
| `FUN_18003e3a0` | `0x18003e3a0..0x18003eaaf` | Complete decoder |
| `FUN_18003f290` | `0x18003f290..0x18003f43f` | Tail encoder |
| `FUN_1800403b0` | `0x1800403b0..0x18004057a` | Tail decoder |
| `FUN_18003f740` | `0x18003f740..0x18003f884` | Relation decoder |
| `FUN_1800405a0` | `0x1800405a0..0x1800406f2` | CRC table builder |
| `FUN_180040700` | `0x180040700..0x1800407d2` | Decode allocation defaults/cap |

## Envelope And Fixed Header

All multibyte wire values are little-endian dwords. The first 75 bytes are
fixed-position framing and header fields:

| Offset | Tag/value | Native owner or profile-9/type-12 value |
| --- | --- | --- |
| `0x00` | tag `87` | Outer envelope |
| `0x01` | raw dword | CRC-32 of bytes `[0x0a, packed_size)` |
| `0x05` | tag `86` | Payload envelope |
| `0x06` | raw dword | `packed_size - 10` |
| `0x0a/0x0b` | `81` / dword | Canonical constant `0x11f248ea` |
| `0x0f/0x10` | `98` / dword | Template type, `12` |
| `0x14/0x15` | `9a` / dword | Rows, `88` |
| `0x19/0x1a` | `9b` / dword | Columns, `104` |
| `0x1e/0x1f` | `91` / dword | Current feature count, live `+0x1c` |
| `0x23/0x24` | `97` / dword | Configured maximum features, live `+0x20`, normally `40` |
| `0x28/0x29` | `92` / dword | Next relation slot/registration count, live `+0x24` |
| `0x2d/0x2e` | `9e` / dword | Maximum of live `+0x14` and every feature's live record count |
| `0x32/0x33` | `9f` / dword | Record allocation limit, live `+0x18` |
| `0x37/0x38` | `9c` / dword | Geometry flag, live `+0x0c`, normally `1` |
| `0x3c/0x3d` | `9d` / dword | Geometry flag, live `+0x10`, normally `1` |
| `0x41/0x42` | `fa` / dword | Queue state, live `+0x8e00` |
| `0x46/0x47` | `fb` / dword | Queue transaction counter, live `+0x8e04` |

For natural type-12 objects both record fields are `150`. They are distinct
live fields: tag `9e` is raised if a feature record count exceeds live `+0x14`,
whereas tag `9f` is copied directly. The two geometry flags are also independent
fields even though the type-12 constructor initializes both to one.

Tag `92` starts at one after the first enrolled feature and advances by the old
feature count on append. A natural nonempty object with `n` features therefore
has `1 + n*(n-1)/2`.

## Features And Relations

The encoder writes exactly `feature_count` tag-`95` elements in physical feature
index order. Their own length dwords delimit them. No separate top-level feature
list wrapper or relation count is present.

Relations follow as zero or more fixed 45-byte records. Tag `93` terminates the
relation sequence, so relation count is the number of consecutive tag-`96`
records and has no separate wire field. One record is:

| Record offset | Field |
| --- | --- |
| `+0/+1` | tag `96`, payload length `40` |
| `+5/+6` | tag `e3`, absolute triangular matrix slot |
| `+10/+11` | tag `e1`, signed leading value |
| `+15/+16` | tag `e4`, signed value 1 |
| `+20/+21` | tag `e5`, signed value 2 |
| `+25/+26` | tag `e6`, signed value 3 |
| `+30/+31` | tag `e7`, signed value 4 |
| `+35/+36` | tag `e8`, signed value 5 |
| `+40/+41` | tag `e9`, signed value 6 |

Tag `e3` is nonnegative matrix indexing state on valid type-12 objects. The
seven relation values are raw signed dwords. Zero-leading relations are emitted;
only a negative leading value is omitted.

When graph flag `f5` is positive, the type-12 encoder visits feature indices in
ascending order, skips reference `f2`, computes pair slot
`feature[max(i,f2)].b6 + min(i,f2)`, and emits each slot whose leading value is
nonnegative. When `f5` is not positive it emits no relations. Natural row bases
are canonical, so this feature traversal also produces strictly increasing
`e3` slots. The generic 50-feature type-12 layout serializes at most 49
relations. A production profile-9 print fixes maximum and current feature count
at no more than 40, so its reachable bound is 39.

The decoder restores records by absolute `e3` slot in encounter order; it does
not sort, deduplicate, or derive endpoints. Natural writers emit a unique
ascending reference star. Production persisted-print validation rejects
duplicates, negative-leading records, nonstar indices, noncanonical row bases,
and inconsistent registration count before use. It accepts reordered unique
star records because the source codec preserves the supplied relation order;
study and enrollment projection restore native ascending order. The complete
matrix, admission, and exact-byte contract is maintained in
`RELATION-MATRIX-AND-PROJECTION.md`.

## Graph Block

The fixed 25-byte graph block is tag `93`, payload length `20`, followed by:

| Relative tag/value offset | Field | Live offset |
| --- | --- | --- |
| `+5/+6` | `f2`, signed graph reference | `+0x87d8` |
| `+10/+11` | `f3`, signed opaque companion | `+0x87dc` |
| `+15/+16` | `f4`, signed opaque companion | `+0x87e0` |
| `+20/+21` | `f5`, graph-established flag | `+0x87e4` |

The constructor values are `-1/-1/-1/0`. Type-12 graph and study code changes
`f2/f5`; no non-codec type-12 owner changes `f3/f4`. The codec preserves all
four raw bit patterns. Natural established objects use an in-range `f2` and
`f5=1`.

## Tail Object

The tail begins immediately after tag `93`. It is 1,333 bytes (`0x535`) on the
wire: tag `94`, payload length `0x530`, and these children:

| Offset from tag `94` | Wire form | Live tail source |
| --- | --- | --- |
| `+5/+6/+10` | tag `a1`, length `200`, payload | `+0x000..+0x0c7` |
| `+210/+211` | tag `a2`, raw dword | `+0x0c8` |
| `+215/+216/+220` | tag `a3`, length `64`, payload | `+0x0cc..+0x10b` |
| `+284/+285/+289` | tag `a4`, length `0x400`, payload | `+0x10c..+0x50b` |
| `+1313/+1314` | tag `a5`, raw dword | `+0x50c` |
| `+1318/+1319` | tag `a6`, raw dword | `+0x510` |
| `+1323/+1324` | tag `a7`, raw dword | `+0x514` |
| `+1328/+1329` | tag `a8`, raw dword | `+0x518` |

The live tail origin is template `+0x87e8`. Only `0x51c` live bytes are
serialized. The adjacent dword at live `+0x8d04`, corresponding to a possible
`+0x51c..+0x51f` extension of a `0x520` local buffer, has no wire field.

Tag `a1` is the complete 50-dword feature-order vector. Its first
`feature_count` entries are the natural traversal permutation; unused entries
remain `UINT32_MAX`. The serializer copies all 200 bytes without sorting.

Tag `a2` is an opaque dword initialized to `-1`. Tag `a3` contains the 22-byte
NUL-terminated string `Milan_v_3.01.09.10.50`; its remaining 42 bytes are
reserved. Tag `a4` is a 1,024-byte reserved block for this profile. At the
current canonical-zero boundary the reserved `a3` suffix and complete `a4`
block are zero, but the DLL decoder and encoder preserve their bytes verbatim.

The trailing dwords have active type-12 owners:

- `a5`, live `+0x8cf4`: accepted study finalization generation; incremented
  after final order generation, with 32-bit wrap;
- `a6`, live `+0x8cf8`: replacement counter; incremented by replacement;
- `a7`, live `+0x8cfc`: append counter; incremented by append;
- `a8`, live `+0x8d00`: derived aggregate footprint/residual scalar.

`FUN_180038810` is the recovered `a8` writer, but Ghidra resolves no direct code
caller. The normal type-12 identify/study path exercised here leaves `a8`
unchanged.

## Size And CRC

For `R` emitted relations and packed feature-element sizes `F[i]`, both
`FUN_18003f510` and `FUN_18003eaf0` use:

```text
packed_size = 1433 + sum(F[i]) + 45*R
payload_size = packed_size - 10
```

The fixed 1,433 bytes are the 75-byte envelope/header, 25-byte graph block, and
1,333-byte tail object. Queue occupancy, queue feature bodies, and queue ranks do
not contribute to the size.

The CRC is CRC-32/ISO-HDLC: polynomial `0x04c11db7`, reflected implementation
constant `0xedb88320`, initial value `0xffffffff`, reflected byte processing, and
final complement. The encoder calls `FUN_1800405a0`, calculates the CRC over
exactly `payload_size` bytes beginning at offset 10 only after the graph and tail
are complete, and writes it at offsets `1..4`. The payload-length dword and both
envelope tags are outside the CRC domain. The decoder builds the same table and
checks the same declared payload domain before allocation.

## Public Ownership And Return Values

`templateUnPack(packed, length, context, out)` accepts a null context. The
natural parity caller uses null, selecting the type-12 defaults of 40 feature
owners and 150 records from `FUN_180040700`, then retaining larger serialized
allocation requirements where applicable. On success it allocates an eight-byte
public handle whose first field owns the decoded `0x8e08` live object. Null
input, output, or zero length returns `0x81`; public-handle allocation failure
returns `0x82`; internal decode statuses are otherwise propagated. The output
handle is assigned only on success.

`templateGetPackedSize(handle)` returns zero for a null handle or null internal
pointer. Otherwise it returns the exact signed 32-bit size above.

`templatePack(handle, output)` returns `0x81` for a null handle, output, or
internal pointer, zero on success, and `0x80` when the internal encoder fails.
It allocates neither public argument and has no output-capacity argument. The
caller must retain the object unchanged between size query and pack.

`templateDelete(handle)` deep-destroys all configured gallery feature owners,
all 20 queue-owner slots, the live object, and finally the public handle. A null
handle or a handle whose first field is null is ignored; in the latter case the
wrapper itself is not freed. A successful delete does not clear the caller's
handle variable.

Decoder failure after live-object allocation invokes `FUN_180037860` before the
public wrapper is freed, so partially allocated feature and queue owners remain
under one cleanup owner.

## Round-Trip And Mutation Boundary

The outer codec preserves valid header, relation, graph, and tail state. A
freshly unpacked object reconstructs queue allocation from `fa` only: state zero
creates 20 empty owners with every rank `-1`, while state one creates none.
Queue bodies, occupancy, and ranks are never serialized. The `fb` counter is
preserved but ordinary type-12 identify/study does not change it.

Whole-template first-pack identity is not guaranteed merely by valid outer
framing. `FUN_18003e3a0` runs post-decode feature normalization before returning,
and that normalization can change feature-owned scalar bytes. A subsequent pack
therefore changes those feature bytes and the dependent outer CRC while leaving
the other outer fields intact. Once this normalization is represented, a second
unpack/pack is byte-identical. This is feature-state canonicalization, not an
alternate outer layout.

Normal identify can change feature-owned state and transient queue occupancy,
but not the serialized header, relation, graph, or tail owners. Its packed
after-match CRC still changes whenever a feature byte changes. Positive study
can change the following outer owners before the single final pack:

- append: tags `91/92`, the feature sequence, possible reference-star relation
  and `f2/f5` state, one `a1` entry, `a7`, and possibly `fa` at capacity;
- replacement: reference-star relation bytes, `a1`, and `a6`;
- every accepted finalized study transaction: sorted `a1` and incremented `a5`;
- graph establishment or reset: relation sequence and `f2/f5`.

The fixed type, geometry, record limits, geometry flags, `a2`, version/reserved
blocks, `a8`, and `f3/f4` do not acquire a normal identify/study writer on this
path.
