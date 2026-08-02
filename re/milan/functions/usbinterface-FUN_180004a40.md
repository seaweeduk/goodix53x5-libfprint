# usbinterface.dll FUN_180004a40

## Audit Scope

Hardware-module routine that references the final calibration log format
containing `tcode`, deltas, `dac_h`, and `dac_l`. This note was created before
first Ghidra decompilation during the scoped production argument provenance
audit.

## Questions

- Which OTP bytes and fallbacks produce `tcode`, `dac_h`, and `dac_l`?
- Where are the resulting values stored for capture use?

## Audit Findings

- Valid OTP derives `tCode = otp[23] + 1` when `otp[23] != 0`.
- `dacHigh = ((otp[17] << 8) | otp[22]) & 0x1ff` and
  `dacLow = ((otp[17] & 0x40) << 2) | otp[31]` when source bytes are valid.
- The values are stored in the hardware calibration structure at `+0x31e`,
  `+0x312`, and `+0x310` respectively.
- Invalid DAC source bytes fall back to `dacHigh` global default and
  `dacLow=0xd0`.
