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
  calibration workspace `+0x1cb64`. When refined output is selected, that
  exported plane and the primary contrast plane remain distinct valid outputs
  of the same preprocessing call.
- The profile-9/type-12 preprocessor emits three extraction auxiliary bytes.
  Byte 0 is the primary clipped-histogram state from `FUN_18004d510`; byte 1 is
  the selected-plane entry value written by this export before
  `FUN_18006d540`; byte 2 is the secondary clipped-histogram state after
  primary/directional promotion. Ordinary WBF enrollment and identify calls
  write entry value zero; only exported argument 7 equal to 1 writes value 2.
  Exact producers and helper arithmetic are in `FUN_18004d510.md`,
  `FUN_18004e0e0.md`, `FUN_18004ef90.md`, `FUN_18004f990.md`, and
  `FUN_18004eba0.md`.
- Before any of those operations, the profile-9 live path applies
  `FUN_180069820` to a copied frame. That helper clamps every sample above
  `0x0fff` before `FUN_1800695e0` tests the two-pixel raw interior. More than
  15% of the samples must be strictly between 100 and `0x0ed8`; equality fails.
  The ready selector comes from initialized global `DAT_180249afc`, not
  exported purpose, setup content, or retained generation state. Failure
  returns `0x29aa` with zero quality/coverage before gain, classification,
  rendering, and extraction.

## Sibling Export Lifecycle

- `preprocessor_init` clears calibrated flag `DAT_180249afc` before invoking the
  setup path. A rejected profile-9 setup therefore returns `0x29bb` with the
  flag still clear; success sets it to 1. The production adapter may retry this
  export once with the same setup frame.
- `preprocessor` refuses a live call unless `DAT_180249afc == 1`. Its output
  descriptor copy is independent of status: a late `0xc351` still has selected
  processed bytes, while early `0x29aa` and `0x7531` returns do not publish a
  processed image.
- `preprocessor_exit` clears `DAT_180249afc` and exactly `0x3048c` bytes at
  calibration workspace `DAT_180219670`. It does not clear gain-ready global
  `DAT_1801efbfc`; `preprocessor_init` also has no xref to that global. Setup
  refresh replaces the workspace from saved calibration and adapter teardown
  resets it; neither path owns the profile-9 gain-ready state.
- The adapter wrapper loads its sensor-keyed calibration blob before
  `preprocessor_init` and saves it immediately after setup succeeds. The blob
  carries workspace sample count `DAT_180219670` but not auxiliary count
  `DAT_1801efbf4`, initialization global `DAT_1801efbf8`, or ready global
  `DAT_1801efbfc`. With a zero loaded sample count, the first live call reaches
  `FUN_1800672e0`, which resets the auxiliary count before the ready check.

The selected-plane entry value also controls retained-reference handling. Value
2 skips retained-reference initialization and update for that call; values 0
and 1 permit the ordinary lifecycle. The selector output may remain
in workspace `+0x26484`, but this export overwrites the entry before the next
ordinary WBF enrollment or identify invocation.

## Instruction Ranges

- Packed flags and argument setup: `0x180002b25..0x180002c44`.
- Result propagation and image copy: remainder of the successful branch.

## Confidence

High for profile and argument provenance.

## Unresolved

- Liveness-switch behavior is outside this milestone.
