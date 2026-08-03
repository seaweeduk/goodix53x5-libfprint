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

## Evidence

- Session validation and handle copy: `0x180001a0c..0x180001a1f`.
- Invalid-session return: `0x180001a2c..0x180001a38`.
- Absence of output validation is visible at the unconditional store through
  `RDI` at `0x180001a1c`.

## Confidence And Unresolved

- Confidence: high for handle indirection, borrowed ownership, and absence of
  graph validation.
- Unresolved: whether callers outside the observed graph rely on retrieval
  before requested-count completion.
