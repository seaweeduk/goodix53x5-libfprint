# preprocessor

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/address: `preprocessor`, `0x180002ab0`.

## Call Graph

- Exported live preprocessing entry point.
- Substantive callee: `FUN_18006d540`.

## Behavior

- Packs the initialized profile globals into chip flags, forwards the caller's
  purpose and runtime selection threshold to `FUN_18006d540`, and copies the
  returned image, quality, and coverage into exported descriptors.
- The profile table, rather than the source descriptor's format byte, supplies
  subtype 12 and the selection-mask selector used by the internal path.
- The exported image is the plane selected by `FUN_18006d540`; it is not a
  generic substitute for all downstream preprocessing planes. Anti-fake
  boundary classification reads the retained primary contrast plane at
  calibration workspace `+0x1cb64`. Sequence 2 selects the refined exported
  plane, so those images differ in `8,351/9,504` bytes while both remain valid
  outputs of the same preprocessing call.
- Native preprocessing now mirrors that ownership explicitly: each successful
  call copies the primary contrast plane into `GoodixMilanPreprocessState` and
  marks it valid only after the complete call succeeds. The selected output API
  and bytes are unchanged; native anti-fake extraction rejects absent state and
  reads the separately owned primary plane.

## Evidence

- Packed flags and argument setup: `0x180002b25..0x180002c44`.
- Result propagation and image copy: remainder of the successful branch.

## Confidence

High for profile and argument provenance.

## Unresolved

- Liveness-switch behavior is outside this milestone.
