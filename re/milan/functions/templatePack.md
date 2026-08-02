# templatePack

## Identity

- DLL: `GoodixEngineAdapter.dll`
- DLL SHA-256: `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`
- Address/body: `0x1800020b0..0x18000215c`.
- Role: exported live-template serializer wrapper.

## Call Graph

- Production callers include `FUN_18002bbf0` and `FUN_18002dde0`.
- Calls `FUN_18003f510` for the packed size and `FUN_18003eaf0` for encoding.

## Recovered Behavior

The first argument is the eight-byte public template handle returned by
`templateUnPack` or `enrolGetTemplate`. The handle's first field is the internal
live-template pointer. The second argument is a caller-owned output buffer.
The export rejects a null handle, null output, or null internal pointer, then
serializes the complete internal object into the output buffer. It does not
allocate or free either public argument.

This matches `milan-manifest.c`: `pack_template` obtains the size using the same
public handle and passes that handle unchanged to `templatePack`.

For the study tail-order contract, `FUN_18003eaf0` passes live template
`+0x87e8` to `FUN_18003f290`, which emits the complete 50-dword order vector as
the fixed 200-byte tag-`a1` payload. Packing does not derive or reorder it; CRC
is computed only after the payload is complete.

For type 12, `FUN_18003f510` and `FUN_18003eaf0` independently traverse the same
live reference star. Graph-established must be positive; self is skipped; pair
slot is `feature[max(i,j)].b6 + min(i,j)`; and only `leading=-1` is omitted.
Zero-leading identity and nonidentity records both serialize. The output order
follows feature indices around the reference, and packed `e3` contains the
matrix slot. Neither wrapper receives recognition evidence.

Native study now performs this projection once after relation mutation and order
finalization, then calls the existing flat codec once. Its focused controls cap
the output at 49 and include every star slot with nonnegative leading value.

## Confidence And Open Questions

- Confidence: high for handle indirection, output ownership, and serialization.
- The export has no output-capacity argument; safety depends on the preceding
  `templateGetPackedSize` result remaining valid until the call.

Cache state `+0x8e00/+0x8e04` is serialized as fixed header tags `fa/fb`, but
the 20 feature bodies and ranks at `+0x8d10/+0x8db0` are omitted. Direct
pack/unpack controls prove occupied entries disappear while state 0 survives.

`fa/fb` are queue enabled/disabled state and queue transaction counter, not
capacity and reference index. Configured maximum/current count are tags `97/91`;
capacity is their equality predicate. Graph reference/established are `f2/f5`
in tag `93`; `f3/f4` are opaque companions. Unit-6 controls hold every other
field fixed while changing each concept independently.

## Action-5 Publication Boundary

The exact queued handoff confirms that production action 5 has no intermediate
published primary template. Primary and queued selectors mutate one live object;
`FUN_1800607b0` and transaction-tail generation run once after queue exhaustion;
this export then serializes that final object. Authoritative sequence 3 closes at
129,303 bytes and SHA-256
`c971239885adccdcaece80b31dc9e5e3abea6b0cc428b1b82307cb05fe12b181`.

Native currently gives its queued matcher an intermediate packed gallery that is
byte-identical to the primary-only finalized DLL result `081c3e...`. Packing is
not itself mutating, but the order/tail finalization performed before that pack
changes subsequent matcher traversal. Native also serializes Q, which loses the
DLL full live-feature boundary. A native gallery bridge may use an internal,
non-published serialization only if it preserves unfinalized order/tail and
continues to match against full prepared/live Q; the validated update candidate
must remain the single post-queue pack.

## Action-0 Exclusion

Unit-5 breakpoint traps record zero calls to this export for shared gate-zero,
outer quality/coverage rejection, code-2 `-11`, positive no-direct, late-enqueue,
capacity-shutdown, and accepted-finalizer-gate-zero action-0 cases. A positive
action-1 empty-queue sanity control records one call, proving the trap is active.
Therefore action-0 transient order/tail/queue state is never serialized by the
production adapter. Manual post-study packing is diagnostic only and must not be
published or persisted.

Native unit 5 does not serialize its action-0 diagnostic state. Sorting and tail
wrap operate directly on the transient scalar representation, which is discarded
after the borrowed observer returns. Positive actions continue through the
existing single final pack path.

## Native Unit-6 Migration

Native metadata now names `fa/fb/f2/f3/f4/f5` by their recovered ownership and
passes them unchanged to the flat codec. Queue state/counter projection is an
explicit final-pack input and is independent of accepted order/tail finalization.
Ten valid scalar controls survive two byte-exact round trips and the official
capacity-reaching append is byte-exact; no valid wire output changed.

## Match-Matrix Exclusion

Neither `180x180` matcher matrix is a live-template field or serialized tag.
Packing retains 32 bytes per type-12 record and omits live record spans
`36..39` and `48..55`; those omitted bytes can affect a later shifted-layout
matrix recomputation after unpack. This is a record-provenance change, not a
matrix cache or serialization rule. Matrix lifetime is documented in
`re/milan/MATCH-MATRIX-CONTRACT.md`.
