# getQuality

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `getQuality`, `0x180002400..0x1800024e5`.
- Substantive callee: `FUN_180044900`.

## Behavior

For a nonnull image descriptor and buffer, the export builds a byte-matrix
header from the descriptor's signed-16 width and height and packs the active
profile globals into chip flags. It calls `FUN_180044900` with:

```text
quality_out  = &outputs[1]
coverage_out = &outputs[0]
```

The public signed-32 array is therefore coverage first and quality second. The
export also writes the low byte of quality to descriptor `+0x28` and the low
byte of coverage to `+0x29`. Final values are already clamped to `0..100`, so
these descriptor writes do not truncate semantic information.

The export has no quality-status return and ignores the internal `0x7532`
base-score result. It also does not apply live preprocessing's final
coverage-`<=5` quality-zero gate. Evidence is the call setup at
`0x1800024a5..0x1800024b7` and descriptor publication at
`0x1800024bc..0x1800024cb`.

Direct byte-image controls make the distinction concrete: quality/coverage is
`94/5` and `95/6`. This proves generic direct-export behavior, not live raw-frame
reachability. Complete profile-9 setup/live controls at coverage 5 and 6 have
producer quality already zero, and no successful raw call with positive
producer quality at coverage `<=5` is established. Confidence is high for the
direct export and caller ownership; live reachability remains unproven.
