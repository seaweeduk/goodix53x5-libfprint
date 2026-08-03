# enrolFinish

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export entry/body: `0x180001a50..0x180001ac2`.
- Role: destroy an enrollment session and its nested live-template owners.

## Call Graph

- Direct callers: `FUN_18002b7e0`, `FUN_18002b960`, `FUN_18002bea0`, and
  `FUN_1800303c0`.
- Direct callees: `FUN_180037860`, `free`, and logging.

## Behavior

The export rejects only a null session pointer. For a valid session it passes
the nested template-owner pointer to `FUN_180037860`, frees that owner, frees the
intermediate handle, clears the session's first qword, and frees the session.
Success returns zero; a null session returns `0x81`.

No graph or completion state is consulted during destruction. The function
assumes `*session` is nonnull and dereferences it before cleanup, so a partially
initialized session does not have a guarded path here.

## Evidence

- Null-session check: `0x180001a65..0x180001a7f`.
- Nested destructor and frees: `0x180001a81..0x180001ab0`.
- Success return: `0x180001abb..0x180001ac2`.

## Confidence And Unresolved

- Confidence: high for ownership order and the missing inner-pointer guard.
- Unresolved: exact subobject ownership performed inside `FUN_180037860`.
