# enrolAddImage

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export entry: `0x180001450`.
- Normal enrollment caller: adapter wrapper `FUN_18002d4b0` at
  `0x18002d62f`. Maintenance helper `FUN_1800303c0` also calls this export at
  `0x18003063d` while constructing a one-feature template for identify.
- Direct enrollment callees: extractor thunk `thunk_FUN_18004ae70`,
  `FUN_1800392f0`, `FUN_180042c30`, `FUN_180041570`, and `FUN_180037b10`.
- Role: process one enrollment scan, update the live template, and publish
  session count, progress, and overlap metrics for the adapter policy.

The addresses `0x180026f60` and `0x180027757` must not be attributed to
`enrolAddImage` in this binary. They are inside EngineTest helpers
`FUN_180026e50` and `FUN_180027240`; the enrollment export is `0x180001450`.

## Native ABI

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
- Normal profile-9 enrollment supplies `0x79`, `0x7d`, and `0xc6`.

## Behavior

- `enrolAddImage` computes a signed residual offset using 32-bit arithmetic:

  ```c
  offset = (int32_t) ((uint32_t) (dac_high - dac_low) * t_code * 0x11b) / 1000;
  if (chip_type == 0x11)
    offset = (int32_t) ((uint32_t) (dac_high - dac_low + 0x55) *
                        t_code * 0x146) / 1000;
  ```

- The normal tuple `0x79,0x7d,0xc6` and chip type `0x0c` yields `-2499`.
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
- The normal mode-0 order before `FUN_180042c30` is: validate nested session,
  image descriptor, 8-bit single-channel format, nonzero frame count, image
  buffer, and output pointer at `0x1800014ae..0x18000151f`; allocate and copy the
  processed frame at `0x18000151f..0x180001578`; publish descriptor quality and
  coverage at `0x180001578..0x18000158c`; require used count below requested at
  `0x18000158f..0x1800015ab`; extract the feature at
  `0x1800015ab..0x1800015f6`; derive anti-fake data at
  `0x1800015fb..0x18000171b`; then call `FUN_180042c30` at
  `0x180001720..0x180001741`.
- Pointer or image-format validation returns `0x81`; processed-frame allocation
  failure returns `0x82`; extraction failure returns `0x80000001`. These exits
  precede stage mutation and used-count increment. A completed mode-0 session
  skips extraction and returns success with its existing state.
- On mode-0 helper success, session `+0x10` receives
  `100 - (stage_metric >> 24)`, used count increments, and session `+0x14`
  receives `100 - (stage_metric & 0xff)` at
  `0x180001796..0x1800017b5`. The right shift is logical and the low metric is
  explicitly masked to one byte; the middle 16 bits do not enter mode-0
  policy. Both complements are stored as dwords and later compared as signed
  dwords by `FUN_18002d4b0`.
- The stage helper accepts insertion 40, then runs graph closure followed by
  type-12 feature normalization before returning success. A subsequent insertion
  41 returns `0x80000005` before feature, relation, graph, order, pair-score, or
  finalizer mutation; progress is zero and the existing 40-feature template is
  unchanged. The exported wrapper's later mode policy cannot turn that rejected
  insertion into an accepted stage.
- Relation construction and anti-fake pair updates are already committed when
  post-insertion policy runs. Mode-0 bad-capture rollback and mode-1 status-4
  rollback remove the last feature through `FUN_180037a30`. Exact retained
  template state is owned by `FUN_180037a30.md`; session count/progress and
  overlap-field ownership are in `enrolDeleteImage.md`.

## Confidence And Unresolved

- Confidence: high for the native ABI, metadata-derived offset, accepted
  count boundary, rollback timing, and 40/41 wrapper behavior.
- The persisted calibration generation used by the normal adapter remains
  unresolved; it does not change enrollment count or rollback ownership.
