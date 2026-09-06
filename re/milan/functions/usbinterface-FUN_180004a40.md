# usbinterface.dll FUN_180004a40

## Identity

- Address: `0x180004a40`
- Logged name: `Milan_CheckSensor`
- Role: read and validate the 32-byte OTP block, then populate profile-9
  calibration fields.

## Ownership And Initialization

`FUN_180017ef8` maps chip-family value `0x220c` (type 12) to profile 9 at
its argument `+0x28`. `device_enable` (`FUN_18000e9b0`) dispatches that
profile to `FUN_1800162ac`, which selects static context `0x1800615f0` and
calls `GxFNHV_MilanOpen` (`FUN_18000450c`). The latter installs this function
at context `+0x18` and publishes the context through `0x1800606d0`.
`device_action` (`FUN_18000e1f0`) action 9 calls that slot without arguments.

The context calibration words are zero-initialized image storage. Neither of
these profile-9 open functions assigns `+0x31c`, `+0x31e`, or `+0x2fe`.
Consequently the conditional non-writes below retain zero on the first check;
this function itself does not clear the calibration structure. Global
high-DAC fallback `0x18005e1c0` has image initializer `0x97` and can retain a
previous successful packed high-DAC value on a subsequent check.

The function reads 32 bytes into a private stack buffer through
`thunk_FUN_18001b2e0` and optionally substitutes file OTP through
`FUN_18001799c` when the first 16 bytes agree. Verification uses
`FUN_180017a84` with profile 9, selecting `FUN_180017918`. Successful
verification precedes calibration writes. The selected block is copied to
global `0x1800606d9` and context `+0x205`, and context `+0x1e8` becomes 1.
The parser calculations read the private block without modifying its bytes.
Before return, `FUN_180007c74` receives global tcode and high DAC. The
successful function result is zero.

## Calibration Fields

- Valid OTP derives unsigned 16-bit `tCode = otp[23] + 1` when
  `otp[23] != 0`, including 256 for byte value 255. A zero byte clears global
  `0x1800606fa` but does not write context `+0x31e`.
- `dacHigh = ((otp[17] << 8) | otp[22]) & 0x1ff` and
  `dacLow = ((otp[17] & 0x40) << 2) | otp[31]` when source bytes are valid.
- The values are stored in the hardware calibration structure at `+0x31e`,
  `+0x312`, and `+0x310` respectively.
- If any of bytes 17, 22, or 31 is zero, both DAC values fall back:
  `dacHigh` uses global `0x18005e1c0`, and `dacLow=0xd0`. Otherwise the
  derived high DAC also replaces that global. Global `0x1800606fe` records
  whether the packed DAC fields were used.
- Nonzero `tCode` writes `dac_delta = 0xc83 / tCode` to context `+0x2fe`
  using unsigned integer division. Zero `tCode` leaves this context word
  unchanged. Four words at `+0x2f6..+0x2fc` receive `dacLow`, and four at
  `+0x2ee..+0x2f4` receive the 16-bit difference `dacLow - dac_delta`, only
  in the nonzero case.

## Profile-9 FDT Thresholds

The function derives unsigned `diff = (otp[17] >> 1) & 0x1f`. When `diff` is
zero it stores `delta_down=13` at context `+0x318`, `delta_up=11` at `+0x31a`,
and `delta_nav=40` at `+0x316`. It clears global `0x1800606fc` but leaves
context `delta_fdt` at `+0x31c` unchanged. Otherwise it computes:

```text
scaled = ((diff + 5) * 0x32) >> 4
delta_down = scaled / 3
delta_up = delta_down - 2
delta_fdt = scaled / 5
delta_nav = ((diff + 5) * 400) / 100
```

Both branches store `delta_img=200` at `+0x314`. The native `delta_fdt`
calculation is `(scaled * 2) / 10`; the expression above is equivalent over
the complete five-bit diff domain. Scaling truncates before division: there
is no rounding-to-nearest. The divisions truncate toward zero over
nonnegative integer inputs. See
`usbinterface-FUN_180014e10.md` for the `delta_fdt` down-event consumer and
`usbinterface-FUN_180014480.md` / `usbinterface-FUN_1800149c4.md` for the
`delta_down` reverse/up consumers, including their separate integer division by
three.
