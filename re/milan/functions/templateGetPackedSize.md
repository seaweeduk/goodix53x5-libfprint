# templateGetPackedSize

## Identity

- DLL: `GoodixEngineAdapter.dll`
- DLL SHA-256: `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`
- Address/body: `0x180002160..0x1800021a6`.
- Role: exported packed-size query for a public template handle.

## Call Graph

- Production callers include `FUN_18002ba60` and `FUN_18002dde0`.
- Delegates to `FUN_18003f510` for the internal live template.

## Recovered Behavior

The export returns zero for a null public handle or null internal pointer.
Otherwise it dereferences the handle once and returns the exact encoded size of
the internal template. The oracle's positive-size and 16 MiB sanity checks are
stricter caller-side bounds; they do not change the DLL ABI.

For type 12 the returned signed 32-bit result is
`1433 + sum(packed_feature_sizes) + 45*emitted_relation_count`. The fixed term
is the complete envelope/header, graph block, and tail. Queue occupancy, queue
feature bodies, and ranks add no bytes. The complete format is maintained in
`../OUTER-TEMPLATE-CODEC.md`.

For type 12, delegated `FUN_18003f510` counts exactly the live reference-star
slots with `leading>=0`, adding 45 bytes per record. This includes zero-leading
identity and nonidentity records and excludes `leading=-1`; it does not receive
or consult recognition evidence. The count loop matches `templatePack`'s
encoder loop instruction-for-instruction at the policy boundary.

## Confidence And Open Questions

- Confidence: high.
- No unresolved behavior affects oracle packing.

## Action-0 Exclusion

`FUN_18002ba60` calls this export only for signed study update `>0`. Unit-5
breakpoint traps record zero calls for every action-0 family and one call for a
positive action-1 sanity control. The action-0 adapter path reuses the old record
size instead; transient order/tail or queue mutations are never sized for
publication. Evidence is under `study-action0-finalization-unit5-v1`.

Native unit-5 diagnostics are fixed-size scalar state and never invoke a packed-
size computation. The focused gate observes zero action-0 update candidates and
one positive action-1 candidate.
