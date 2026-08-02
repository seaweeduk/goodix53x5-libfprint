# identifytemplate

## Binary And Body

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `identifytemplate`, `0x1800024f0..0x180002632`.
- Role: compare the features of one unpacked candidate enrollment object with
  an unpacked enrolled template.

## Call Graph And Behavior

- Production caller `FUN_18002dde0` uses this for enrollment-template
  comparison, not normal image verification.
- The export copies the candidate object's `0x8e08`-byte feature-pointer table,
  iterates its features, and calls `FUN_18005edb0` with a stack-local `0x698`
  evidence object. It returns the first positive feature index or `UINT32_MAX`.
- It does not write retained-probe global `0x18024ebe8`, matched-template global
  `0x18024e548`, or evidence global `0x18024e550`. Consequently calling
  `templateStudy` after this export cannot reproduce the production
  `identifyImage` study contract.

## Evidence And Confidence

- The local evidence object is at `[rsp+0x50]`; the matcher call is
  `0x1800025cf`; the positive-index write is `0x180002628`.
- Ghidra xrefs show the three study globals are written/read only by
  `identifyImage` and `templateStudy`, not this export.
- Confidence is high. The opaque third exported argument is unused on the
  profile-9/type-12 path and is not relevant to the authority decision.
