# usbinterface.dll FUN_180005200

## Audit Scope

DAC-selection routine that logs `tcode_use`, `dac_delta`, register DAC,
`dac_l`, and `dac_h`. This note was created before first Ghidra decompilation
during the scoped production argument provenance audit.

## Questions

- Can the runtime DAC-high value differ from its OTP-derived default?
- Which switch and measurements control that adjustment?

## Audit Findings

- Reads sensor DAC register `0x220` and records the result in the hardware
  calibration structure.
- OTP `tCode` controls the configured DAC delta; mode selects constants `0x28`
  or `0x1a` in the adjacent calibration field.
- The package's `HVDacAdjustSwitch` compiled default is `0`.
