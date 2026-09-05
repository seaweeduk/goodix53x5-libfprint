# usbinterface.dll FUN_180014ce8

## Identity

- Address: `0x180014ce8`
- Role: validate a TX-on/TX-off image-base pair by interior mean absolute
  difference.

## Recovered Behavior

```c
bool image_base_pair_valid(uint8_t rows, uint8_t columns,
                           const uint16_t *tx_on,
                           const uint16_t *tx_off,
                           uint16_t threshold)
{
  uint64_t sum = 0;
  for (int y = 2; y < rows - 2; y++)
    for (int x = 2; x < columns - 2; x++)
      sum += abs(tx_on[y * columns + x] - tx_off[y * columns + x]);
  return sum / ((rows - 4) * (columns - 4)) < threshold;
}
```

## Evidence

- `R12D` supplies the initial column 2 at `0x180014d3b`. Subsequent inner
  iterations load the incremented column counter `DI` into `EAX` at
  `0x180014d5c` and jump to `0x180014d3e`, after that initialization.
  Each iteration therefore adds the row offset to the advancing column;
  the loop does not repeatedly sample column 2.

- Sole relevant production caller: `FUN_180015c60`, call at `0x180015e7c`.
- At that call, Microsoft x64 argument storage is `CL=rows` from context
  `+0x1f0`, `DL=columns` from `+0x1f1`, `R8=first/TX-on image`,
  `R9=second/TX-off image`, and stack argument 5 is threshold `+0x314`.
- The inner subtraction is first minus second at
  `0x180014d44..0x180014d55`, followed by branchless absolute value. The
  absolute value makes the result symmetric but does not make caller argument
  provenance ambiguous.
- The sum is divided by `(rows - 4) * (columns - 4)` at
  `0x180014d71..0x180014d91`; `0x180014d9c..0x180014db2` implements the strict
  unsigned comparison `average < threshold`.
- The threshold is hardware-context word `+0x314`, populated during hardware
  calibration setup. `FUN_180004a40` writes decimal `200` in both OTP branches.
- The helper returns only a boolean. It does not combine, average, subtract, or
  otherwise produce a reference frame.
- It performs no writes through either image pointer.
