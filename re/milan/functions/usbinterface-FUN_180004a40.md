# usbinterface.dll FUN_180004a40

## Identity

- Address: `0x180004a40`
- Logged name: `Milan_CheckSensor`
- Role: read and validate the 32-byte OTP block, then populate profile-9
  calibration fields.

## Calibration Fields

- Valid OTP derives `tCode = otp[23] + 1` when `otp[23] != 0`.
- `dacHigh = ((otp[17] << 8) | otp[22]) & 0x1ff` and
  `dacLow = ((otp[17] & 0x40) << 2) | otp[31]` when source bytes are valid.
- The values are stored in the hardware calibration structure at `+0x31e`,
  `+0x312`, and `+0x310` respectively.
- Invalid DAC source bytes fall back to `dacHigh` global default and
  `dacLow=0xd0`.

## Profile-9 FDT Thresholds

The function derives unsigned `diff = (otp[17] >> 1) & 0x1f`. When `diff` is
zero it stores `delta_down=13` at context `+0x318`, `delta_up=11` at `+0x31a`,
and `delta_fdt=0` at `+0x31c`. Otherwise it computes:

```text
scaled = ((diff + 5) * 0x32) >> 4
delta_down = scaled / 3
delta_up = delta_down - 2
delta_fdt = scaled / 5
```

The divisions truncate toward zero over nonnegative integer inputs. See
`usbinterface-FUN_180014e10.md` for the `delta_fdt` down-event consumer and
`usbinterface-FUN_180014480.md` / `usbinterface-FUN_1800149c4.md` for the
`delta_down` reverse/up consumers, including their separate integer division by
three.
