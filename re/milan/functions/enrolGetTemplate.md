# enrolGetTemplate

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export entry/body: `0x1800019f0..0x180001a47`.
- Role: expose the enrollment session's live public template handle.

## Call Graph

- Direct callers: `FUN_18002d8b0` at `0x18002d9ca` and `FUN_1800303c0` at
  `0x180030671`.
- Direct callee: logging only.

## Behavior

For a nonnull session whose first nested pointer is nonnull, the export copies
that nested handle's first qword to the caller output and returns zero. This is a
borrowed live public template handle; no allocation, copy, or ownership transfer
occurs. Invalid session pointers return `0x81`.

The output pointer is not validated. The export also performs no count,
relation, reference, graph-established, or completion validation. Consequently
`FUN_18002d8b0` can retrieve a graphless template once its separate count gate
declares enrollment complete.

That caller's gate is session used count versus requested count, not live
template count versus physical maximum. For profile 9/type 12, the default
adapter carries Glass configuration count 12 through context `+0x8a` and writes
it to session required count `+0x08` before enrollment begins. The resulting
12-feature handle is therefore borrowed and packed before the 40-feature
`FUN_180042c30` capacity finalizers. A session explicitly configured for 8 uses
the same accessor behavior, but that is a legacy/nondefault control rather than
the production count. Graph establishment on the last requested stage is visible
immediately because the stage update writes `f2/f5` before returning; it is not
deferred to this accessor or to packing.

If the first graph is established by an insertion at the requested count and
adapter policy subsequently deletes that incoming feature, deletion retains the
graph whenever its reference is an earlier feature. The borrowed handle can
therefore have `f5=1`, one active surviving reference, and no serializable
reference edge while its used count has returned below the request. Retrieval
still succeeds; only the completion caller's separate count gate prevents
publication as a completed enrollment. A later accepted retry can restore the
newest reference edge without establishing the graph a second time.
The retained graph and pair-score ownership belong to `FUN_180037a30`;
`enrolDeleteImage` owns the corresponding used-count/progress rewind.

## Evidence

- Session validation and handle copy: `0x180001a0c..0x180001a1f`.
- Invalid-session return: `0x180001a2c..0x180001a38`.
- Absence of output validation is visible at the unconditional store through
  `RDI` at `0x180001a1c`.
