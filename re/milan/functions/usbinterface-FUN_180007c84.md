# usbinterface.dll FUN_180007c84

## Audit Scope

Routine that logs `tcode`, calibrated `dac_h`, and default `dac_h`. This note
was created before first Ghidra decompilation during the scoped production
argument provenance audit.

## Questions

- When is calibrated DAC-high committed to the capture calibration structure?
- What value remains when DAC adjustment is disabled or fails?

## Audit Findings

- Dynamic adjustment runs only when its boolean argument is nonzero.
- When active, it updates the pointed DAC value from the adjusted register
  result. When disabled, it leaves the OTP-derived value unchanged.
