# enrolFinish

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900.
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

## Separate Commit And Calibration Save

Session destruction is not the calibration-persistence boundary.
`EngineAdapterCommitEnrollment` (`FUN_180022700`) performs record storage through
`FUN_180025930` and, only when that result is nonnegative, calls
`FUN_180030b40(context + 0x4a)` at `0x180022af4`, then `FUN_180031120`.
The first callee saves the already retained calibration workspace under the
16-byte sensor identity by calling `FUN_18002aef0`. It translates save failures
to `0x8000ffff`, but CommitEnrollment does not assign that return value to its
own HRESULT; it retains the record-storage result. A failed record-storage
result skips the save.

The packet's pre-extraction snapshot and completed-sample refresh ownership are
described in `FUN_180031d00.md`: a finger-up reference refresh alone cannot
replace it before commit. Neither `enrolGetTemplate` nor this destructor refreshes
or serializes preprocessing state.
