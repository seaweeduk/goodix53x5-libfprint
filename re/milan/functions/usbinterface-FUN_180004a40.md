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
`device_action` (`FUN_18000e1f0`) action 9 calls that slot without arguments
at `0x18000e32e`. It retains the callback result at `0x18000e898` and
returns it after leaving the critical section while context `+0x204`
remains enabled; a disabled context instead returns `0xffffffff`.

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

## OTP Integrity And Admission

The profile selector passed to `FUN_180017a84` at `0x180004c37` is the
32-bit context field `+0x228`, not the USB product ID. `FUN_1800162ac`
copies its argument's profile field `+0x28` to static context `+0x228` at
`0x180016369..0x18001636c`. The dispatch compares this value with 9 at
`0x180017aa8` and calls `FUN_180017918` at `0x180017ab1`.

`FUN_18001b2e0` requests category `0x0a`, command 3, with two zero payload
bytes and copies the response bytes from `0x1800638d3` to its caller's
buffer. This caller supplies length 32. No byte swapping occurs between
that copy and the integrity verifier.

`FUN_180017918` constructs a private 31-byte hash input in this order:
`otp[0..24]`, then `otp[26..31]`. Its loads/stores at
`0x180017935..0x180017960` preserve byte order; byte 25 is excluded, not
replaced by zero. It calls `FUN_18002d5e0` with byte count `0x1f`.
That helper uses the 256-byte table at `0x180059c70`, the non-reflected
CRC-8 table with polynomial `0x07`:

```text
state = 0
for each input byte in order:
    state = table[state XOR byte]
hash = (~state) & 0xff
```

Each table lookup zero-extends its byte result. The helper complements EAX
at `0x18002d60b`; only AL is compared with `otp[25]` at `0x18001796b`.
The verifier returns zero on equality and `0xffffffff` on mismatch. It
does not modify the input before comparison. The corresponding current
helpers are `goodix_device_compute_otp_hash` and `goodix_device_verify_otp`
in `device/calibration.c`.

After equality, the verifier clears bytes 26, 27, and 28 only when global
byte `0x1800637d8` equals exactly 1 (`0x180017970..0x18001797e`). This
global has zero image initialization; the profile-9 verifier and dispatch
do not write it. The conditional clearing happens after hashing and does
not recompute byte 25. The caller publishes the resulting block, including
any such clearing, only on success. The calibration expressions below
consume bytes 17, 22, 23, and 31, not the conditionally cleared bytes.

On mismatch, `FUN_180017918` leaves the block unchanged, but dispatch
`FUN_180017a84` writes little-endian word `0xa55a` at bytes 10..11 and
bytes `f1 fb 09` at 26..28 (`0x180017b55..0x180017b61`), retaining failure
status. These writes affect the private caller buffer, not the published
OTP copies. `Milan_CheckSensor` branches on nonzero verifier status at
`0x180004c4e`: it clears context dword `+0x1e8`, writes bytes
`09 fb f1` to context `+0x1f8..+0x1fa`, skips OTP publication and all
calibration calculations, and returns `0xffffffff`. Success instead
publishes both 32-byte copies and sets `+0x1e8=1` before calibration.

After either integrity result, `FUN_180007c74` copies global tcode and
high DAC into words `0x180060cb8` and `0x180060cbc`; failure does not derive
new calibration values from the rejected block. A failed OTP read returns
before verification, publication, these status writes, or this final copy.

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
