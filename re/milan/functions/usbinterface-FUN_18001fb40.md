# usbinterface.dll FUN_18001fb40

## Audit Scope

Hardware-module routine that references the capture log format
`tcode_use`, `dac_h`, and `dac_l`. This note was created before first Ghidra
decompilation during the scoped production argument provenance audit.

## Questions

- Which calibration fields are emitted into the sample-data trailer?
- Are values adjusted per capture before the engine receives them?

## Audit Findings

- `CaptureFramedone` copies calibration words `+0x31e`, `+0x312`, and `+0x310`
  directly into the sample trailer as `tcode_use`, `dac_h`, and `dac_l`.
- There is no per-capture arithmetic at this boundary.
- Effective callback arguments are device/context, retained image base, live
  frame, live-frame byte length, and a one-byte base-refresh marker.
- At `0x18001fcbc..0x18001fcd9`, the second argument is copied as one
  `rows * columns * 2` frame into staging address `0x180093508`. In the engine's
  image-payload view this is offset `+0xebf0`, the frame passed to
  `preprocessor_init`.
- At `0x18001fca0..0x18001fcb7`, the third argument is copied to payload
  offset `+0x03`. The fifth argument is stored at payload offset `+0x00` and is
  the one-shot marker consumed by `GoodixEngineAdapter.dll` `FUN_18001f610`.
- This routine forwards a previously retained hardware-context frame; it does
  not capture, average, or transform a new reference.
