# enrolDeleteImage

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export entry/body: `0x180001960..0x1800019e6`.
- Role: mode-0 public rollback of the most recently accepted enrollment image.

## Call Graph

- Direct caller: `FUN_18002d4b0` at `0x18002d818`.
- Direct callees: `FUN_180037a30` and logging.

## Behavior

Outside mode 1, the export validates the session's nested template pointers,
calls `FUN_180037a30` to remove the last live feature and repair relation/graph
state, decrements session used count `+0x0a` when positive, and recomputes
progress as `min(used * 100 / requested, 100)`. Invalid pointers return `0x81`.
The helper return is ignored.

When global enrollment mode equals 1, the export immediately returns without
setting `RAX`; mode-1 deletion instead calls `FUN_180037a30` directly from
`FUN_180041570`. The production adapter caller reaches this export from its
mode-0 bad-capture rollback gate.

## Evidence

- Mode split: `0x180001966..0x180001972` and `0x1800019e1`.
- Nested-pointer validation: `0x18000197e..0x180001999`.
- Deletion and progress rewind: `0x18000199b..0x1800019c8`.
- Invalid-pointer return: `0x1800019d0..0x1800019dc`.

## Confidence And Unresolved

- Confidence: high for the mode-0 rollback and progress update.
- Unresolved: whether the indeterminate mode-1 return is intentional ABI
  behavior or a compiler artifact from a source path with no explicit return.
