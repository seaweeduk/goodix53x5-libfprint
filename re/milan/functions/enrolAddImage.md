# enrolAddImage

## Audit Scope

Exported enrollment update at `0x180001450`, reached from the production adapter
wrapper `FUN_18002d4b0`. This note was created before new Ghidra inspection for
the production anti-fake metadata audit.

## Known Production ABI

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
- Historical oracle runs supplied zero metadata and remain synthetic. The
  current corrected oracle supplies the production tuple and nonnull auxiliary,
  but still initializes a controlled calibration generation rather than
  reproducing unknown persisted Windows state.

## Questions

- Where does the export retain or forward each of `t_code`, `dac_high`, and
  `dac_low`?
- Which anti-fake stage combines those values with the retained calibration
  workspace and raw frame?
- Is the current residual expression's single `t_code` subtraction actually a
  packed or derived scalar involving all three metadata values?
- Why do production and zero-metadata anti-fake feature records first diverge at
  feature offset 1049 while preprocessing, primary records, and relations remain
  exact?
- Which anti-fake lifecycle/security fields and pair scores depend on the
  production metadata tuple?
- Can native code reproduce the generic arithmetic directly, without a
  profile-9 tuple special case or matcher-threshold changes?

## Audit Findings

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
- The native API now accepts all three metadata values and derives the offset
  generically. No production tuple or matcher threshold is embedded in the
  implementation.
- The remaining instrumented/uninstrumented difference is the undefined
  one-past feature-mask byte documented in `FUN_1800392f0.md`. Independent
  corrected TX-on runs observed both active and inactive allocator residue.
  Neither observation is a byte authority. Native always supplies semantic zero
  under `canonical-zero-v1` before complete anti-fake construction. Paired
  zero/nonzero output remains diagnostic only and cannot request an enrollment
  retry or prevent stage advancement.

## Authoritative Artifacts

- Corrected uninstrumented packed template:
  `re/milan/private/profile9-txon-corrected-20260723/enroll8-minimal-repeat/artifacts`
- Instrumented preprocessing and internal anti-fake stages:
  `re/milan/private/profile9-txon-corrected-20260723/enroll8-detail/artifacts`
- Superseded TX-off fixtures remain under
  `re/milan/private/real-learning-profile9-production-20260723/`.

## Validation

- Native preprocessing, feature records, and relations remain exact for all
  eight captures.
- Each native anti-fake payload equals the corresponding uninstrumented
  production feature payload.
- Native anti-fake lifecycle/security fields and all 28 pair scores equal the
  uninstrumented production template behavior.
- No matcher threshold is changed.
- The production eight-capture gate reports exact preprocessing, all feature
  records and relations, all eight anti-fake payloads, lifecycle scores and
  flags, all 28 pair scores, and `enrollment_template_byte_exact=1,mismatches:0`
  for all 99,684 bytes.
