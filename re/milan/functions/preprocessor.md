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
- The existing Linux retained profile-9 fields mirror the preprocessor
  broken-level/reference path, not the later extraction classifier in
  `FUN_180048260`. In particular, the capped-50 reference count and capped-5
  component-age count are not the extraction three-plane count or its separate
  hysteresis. The extraction-specific state and implementation boundary are
  mapped in `FUN_180048260.md`.
- The profile-9/type-12 preprocessor emits three extraction auxiliary bytes.
  Byte 0 is the primary clipped-histogram state from `FUN_18004d510`; byte 1 is
  the selected-plane scalar saved at live-call entry; byte 2 is the secondary
  clipped-histogram state after primary/directional promotion. Exact producers
  and helper arithmetic are in `FUN_18004d510.md`, `FUN_18004e0e0.md`,
  `FUN_18004ef90.md`, `FUN_18004f990.md`, and `FUN_18004eba0.md`.
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
  processed bytes, while checker-visible `0x29aa` and `0x7531` retry artifacts
  remain nullable according to the runtime lifecycle contract.
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
  `FUN_1800672e0`, which resets the auxiliary count before the ready test.

## Linux Auxiliary Mapping

Current source implements these values from the following planes:

| Native input/state | Existing Linux source |
| --- | --- |
| prepared setup/live difference | local `difference` when `setup_map[0] >= 4096`; otherwise recompute `max(setup_map[i] - normalized_live[i] + 3000, 0)` from the two existing source planes as in `FUN_18004ee80` |
| eight-edge-eroded active mask | local `valid` in `goodix_milan_profile9_build_broken_mask()` |
| Q16 `[6980,51576,6980]` filtered plane | local `blurred` in `goodix_milan_profile9_build_broken_mask()` |
| active reference count | `admitted_pixels`, or count nonzero `contrast_mask` |
| current class plane/counts | `broken_mask` before classes 1/2 are cleared and `profile9_class_counts` |
| retained reference/support state | `profile9_history_reference`, `profile9_reference_age`, and `profile9_history_count` |
| first retained-reference source/count | `calibration_map` and `sample_count` |
| retained component state | `profile9_component_age` and `profile9_history_update_count` |
| prior selected plane | snapshot `state->selected_refined` at function entry |
| directional raw plane | `normalized_live` |

`goodix_milan_profile9_build_broken_mask()` computes primary state while
`blurred`, `valid`, and the pre-clear class plane are available, computes the
secondary/fallback state from retained reference/support state, computes the
directional state from `normalized_live` and `valid`, and stores the tuple in
`state->extraction_auxiliary`. This occurs before
`goodix_milan_preprocess()` replaces `state->selected_refined` with the current
selection, so byte 1 retains the entry snapshot exactly.

The entry snapshot is also a state-machine input: prior selected-plane value 2
skips retained-reference initialization/update for the call, while values 0/1
permit it. Current source threads that snapshot through
`milan_profile9_prepare_history()` and the auxiliary-byte handoff.

Current source also implements the native first-initialization branch: when
`profile9_history_count == 0`, `sample_count > 1`, and the prior selector is not
2, copy `calibration_map` to `profile9_history_reference`, set history count to
`min(sample_count, 21)`, fill `profile9_reference_age[]` with that count, and
suppress the secondary fallback for that call.

## Current Input Audit (2026-08-09)

Current top has corrected the former raw-input ordering defect: it clamps copied
setup/live frames before applying live admission and no longer rejects an input
solely for an over-`0x0fff` sample. It still does not model profile-9 setup
validation. The DLL requires strictly more than 85% of the 8,736-pixel setup
interior in `(100, 0x0ed8)` before setting ready and returns `0x29bb` otherwise;
current generation publication checks only frame size and TX-on/TX-off MAD.
Exact instructions, boundary controls, and reachability are recorded in
`FUN_1800695e0.md` and `FUN_180069820.md`.

Fresh retained-frame controls on 2026-08-20 are exact at this boundary. The
approved DLL and production-head current source both return status/quality/
coverage `0/90/99` and processed SHA-256
`fdb3459aa40d85a71377ea9fed60f485298b0c4f9be61844f040a5627e9f1c03`
for the successful control. Both return `0x29aa/0/0`, expose no processed image,
and leave extraction unattempted for the raw-admission control. Committing that
`0x29aa` as a prelude does not perturb the successful target on either backend:
the target status, metrics, processed image, and extracted probe remain exact.
This validates the checker replay rule for the early retry without generalizing
to the later stateful `0x7531` path.

## Evidence

- Packed flags and argument setup: `0x180002b25..0x180002c44`.
- Result propagation and image copy: remainder of the successful branch.

## Confidence

High for profile and argument provenance.

## Unresolved

- Liveness-switch behavior is outside this milestone.
