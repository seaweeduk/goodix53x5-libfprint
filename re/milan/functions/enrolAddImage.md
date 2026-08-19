# enrolAddImage

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export entry: `0x180001450`.
- Production caller: adapter wrapper `FUN_18002d4b0`.
- Role: process one enrollment scan, update the live template, and apply
  session-level acceptance or rollback policy.

## Production ABI

```c
int32_t enrolAddImage(
  void *session,
  void *image_descriptor,
  void *feature_auxiliary,
  uint32_t quality_coverage[2],
  void *calibration_workspace,
  const uint16_t *raw_frame,
  uint16_t t_code,
  uint16_t dac_high,
  uint16_t dac_low);
```

- `FUN_180032990` proves the feature auxiliary is the engine-owned, zeroed
  `0x4c98` object at context `+0x198`.
- `FUN_18002d4b0` proves arguments 5 and 6 are the retained calibration
  workspace and retained raw frame, followed by the three sensor metadata
  values.
- `FUN_18001f610` proves the metadata values originate in the sample trailer and
  are retained independently as `tCode`, `dacHigh`, and `dacLow`.
- Production profile-9 enrollment supplies `0x79`, `0x7d`, and `0xc6`.
- Zero-metadata oracle inputs are synthetic. The production tuple and nonnull
  auxiliary still do not reproduce unknown persisted calibration state.

## Behavior

- `enrolAddImage` computes a signed residual offset using 32-bit arithmetic:

  ```c
  offset = (int32_t) ((uint32_t) (dac_high - dac_low) * t_code * 0x11b) / 1000;
  if (chip_type == 0x11)
    offset = (int32_t) ((uint32_t) (dac_high - dac_low + 0x55) *
                        t_code * 0x146) / 1000;
  ```

- The production tuple `0x79,0x7d,0xc6` and chip type `0x0c` yields `-2499`.
- The export passes the retained calibration workspace, retained raw frame,
  packed chip information, live feature object, and derived offset to
  `FUN_1800392f0`. The three metadata values are not independently retained in
  the anti-fake object.
- `FUN_1800392f0` passes calibration map `+0x9924`, raw frame, rows, columns,
  derived offset, and chip type to `FUN_18003a150`.
- `FUN_18003a150` computes each interior residual as
  `calibration - raw - offset - 0x1bb7`; chip types `0x0c` and `0x11` then copy
  adjacent interior values to the border.
- After successful `FUN_180042c30`, mode 0 increments session used count `+0x0a`
  at `0x1800017ae` and derives progress from used/requested at
  `0x180001887..0x18000189d`. It also forces progress to 100 if the live
  template itself reaches physical maximum at `0x1800018a0..0x1800018aa`, but
  these are distinct predicates: normal profile-9 adapter completion at 12 can
  pack a live template whose physical maximum is 40 and whose capacity
  finalizers have not run.
- A `FUN_180042c30` failure branches to cleanup at
  `0x180001741..0x180001781` before the `+0x0a` increment, so a rejected stage
  does not count as accepted. The adapter maps other nonzero returns to `0x8010`
  at `0x18002d6bd..0x18002d6cc`, also without changing used count. Its later
  bad-capture gate is a provisional-acceptance rollback: it calls
  `enrolDeleteImage` at `0x18002d818`, which decrements `+0x0a` at
  `0x1800019a0..0x1800019b0` and recomputes progress before the adapter reports
  `0x8008` at `0x18002d825`. Rejected or rolled-back scans therefore require
  another scan; they do not reduce the default Glass requirement of 12 accepted
  stages. Physical touches can therefore exceed 12.
- The stage helper accepts insertion 40, then runs graph closure followed by
  type-12 feature normalization before returning success. A subsequent insertion
  41 returns `0x80000005` before feature, relation, graph, order, pair-score, or
  finalizer mutation; progress is zero and the existing 40-feature template is
  unchanged. The exported wrapper's later mode policy cannot turn that rejected
  insertion into an accepted stage.
- Relation construction and anti-fake pair updates are already committed when
  post-insertion policy runs. Mode-0 bad-capture rollback and mode-1 status-4
  rollback remove the last feature structurally through `FUN_180037a30`; they
  do not restore pair scores changed on prior features during that accepted
  stage.
- The undefined one-past feature-mask read is owned by `FUN_1800392f0.md`.
  Allocator residue at that byte is not semantic evidence and cannot request a
  retry or prevent stage advancement.

Requested-8 oracle evidence exercises a legacy/nondefault session control. It
does not change the normal Glass requirement of 12 accepted stages and does not
exercise the physical-capacity finalizers at 40.

## Confidence And Unresolved

- Confidence: high for the production ABI, metadata-derived offset, accepted
  count boundary, rollback timing, and 40/41 wrapper behavior.
- The persisted calibration generation used by production remains unresolved;
  it does not change enrollment count or rollback ownership.
