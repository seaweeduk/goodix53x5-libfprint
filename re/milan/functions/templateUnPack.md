# templateUnPack

## Identity

- DLL: `GoodixEngineAdapter.dll`
- DLL SHA-256: `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`
- Address/body: `0x1800021b0..0x180002284`.
- Role: exported packed-template decoder wrapper.

## Call Graph

- Callers: `FUN_180011530`, `FUN_18002dde0`, and `FUN_18002e360`.
- Callees: `FUN_18003e3a0`, `FUN_18000b9d0`, `malloc`, `free`, and the
  security-cookie helper.

## Behavior

The four effective arguments are packed bytes, byte length, unpack context, and
an output handle. The function validates non-null input/output and nonzero
length, allocates an eight-byte public handle, and delegates all decoding to
`FUN_18003e3a0`. On success the handle owns the decoded live-template pointer;
on failure it frees the handle and propagates the decoder status.

Production callers store these public handles directly in candidate arrays.
`identifyImage` dereferences each candidate-array element once to obtain the
internal live-template pointer. The oracle's `candidate_data`/`candidate`
construction preserves exactly that two-level layout while retaining the
original public handle for packing and deletion.

Tail decoder `FUN_1800403b0` restores tag `a1` into live template `+0x87e8`.
Valid production templates carry exactly 200 bytes, so chronological study
reloads the prior persistent gallery order rather than reconstructing it from
feature storage.

The decoder does not validate the defined order prefix. A focused 40-feature
control with a duplicate prefix entry decoded and completed identify/study at
score 25/action 4. Replacing the first entry with 40 also decoded, then caused
an unhandled read page fault at address `0x150` during matcher traversal. The
safe persisted-input contract is therefore stricter than DLL decode: the first
`feature_count` dwords must be an in-range permutation before the live template
can reach matching or study. See
`../PERSISTED-ORDER-ADMISSION-AUDIT-20260809.md`.

The delegated decoder initializes the live relation matrix to unset identity,
restores feature scalar `b6` row bases, and writes each packed tag-`96` record to
absolute slot `e3`. Tail tags `f2` and `f5` restore the live reference and
graph-established state. Packed relations are therefore sufficient to rebuild
the serializer-visible reference star, while omitted non-star/unset slots remain
sentinel identity.

Evidence is the decompile at `0x1800021b0`, especially the call to
`FUN_18003e3a0` and the success assignment at `0x18000225f..0x180002274`.

Native study now mirrors this boundary in a local 1,225-slot matrix, validates
all feature `b6` row bases and packed `e3` indices, and initializes every omitted
slot to the unset identity before applying study mutations.

## Confidence And Open Questions

- Confidence: high for wrapper ownership and error flow.
- The semantic type of the third argument remains unresolved.

For the type-12 cache queue, unpack restores only scalar state/counter tags
`fa/fb`. State 0 allocates 20 empty cache feature owners and resets every rank to
`-1`; state 1 allocates none. Packed cache entries never cross this boundary.

Unit-6 direct controls prove nonzero `fb` is valid and survives independently:
7 and `UINT32_MAX` both round-trip for state 0, and 7 round-trips for state 1.
They also prove exact-capacity `8/8` can unpack with `fa=0`, while below-capacity
`8/9` can unpack with `fa=1`. Fresh unpack reconstructs queue allocation from
`fa` only and reconstructs capacity from count/maximum only.

The DLL accepts noncanonical `fa=2` and `f5=2`; an out-of-range stale `f2`
caused a post-decode access fault. A native codec must fail closed on those
unsafe states rather than copy the DLL's permissive/crashing behavior.

Native unit 6 implements that boundary. It accepts every `fb` bit pattern,
preserves opaque signed `f3/f4`, and accepts unestablished `f2=-1` or an in-range
stale index. It rejects noncanonical queue/graph flags, impossible count/maximum,
and established or stale out-of-range references before any graph consumer can
dereference them. Queue owners/ranks are reconstructed only in the separate
transient queue object and must validate before enqueue or consumption.

## Match-Matrix Reconstruction Boundary

Unpack does not restore either matcher matrix. Reconstructed type-12 live
records zero the spans omitted by the 32-byte packed representation, including
`36..39` and `48..55`. A later ordinary matcher call allocates fresh matrices
and computes them from those reconstructed 56-byte records, so shifted producer
or consumer layouts can differ from an otherwise related full-live object.
Native must treat this as input provenance and must not preserve stale matrices
across unpack.
