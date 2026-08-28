# usbinterface.dll FUN_180015c60

## Identity

- Address: `0x180015c60`
- Body: `0x180015c60..0x18001609a`
- Logged name: `MilanHV_update_allbase`
- Role: acquire and validate the profile-9 FDT and image bases.

## Acquisition Sequence

For profile 9 / sensor type 12, the function allocates two full-size 16-bit
image buffers and performs:

1. Invoke mode `4` through callback slot `+0x40` at
   `0x180015d2b..0x180015d3a`. Profile 9 installs `FUN_1800059c0`, which calls
   `FUN_180005094` (`Milan_DlCfg`) to build and upload the OTP-patched 256-byte
   sensor configuration.
2. Read an FDT sample with TX enabled through callback `+0x160` at
   `0x180015d58`.
3. Capture a no-finger image through callback `+0x158` at `0x180015d91`, using
   low DAC `+0x310`, TX enabled, and HV enabled.
4. Read an FDT sample with TX disabled at `0x180015dad`.
5. Validate the first TX-on FDT sample against the TX-off sample with
   `FUN_180014c98`.
6. Capture a second no-finger image at `0x180015e4f` with the same DAC and HV
   settings but TX disabled.
7. Validate the image pair through `FUN_180014ce8` at `0x180015e7c`, using
   threshold word `+0x314`.
8. If the image pair is admitted, read a second TX-on FDT sample at
   `0x180015ed7` and validate it against the prior TX-off sample.
9. On complete admission, retain only the first TX-on image at `+0x248`, set
   base-valid bytes `+0x232/+0x233`, and set image-valid byte `+0x237`.

The final TX-on manual response's touch-flag word is not an output of callback
slot `+0x160` and is not retained by this function. Only its 12 FDT words reach
the validation at `0x180015efa`. A present-finger vector that fails this final
comparison therefore selects the common failure postlude without creating a
software touch snapshot or an FDT-up recovery state.

`FUN_180005094` applies the profile-9 OTP patches before calling
`thunk_FUN_18001aed8` (`Dlcfg`) with length `0x100` and timeout `500`.
`FUN_18001aed8` retries one unsuccessful category-9 configuration download, for
at most two command attempts. Final failure returns `-1` before FDT/image
acquisition.

## Capture ABI And Settings

Profile 9 installs `FUN_1800055d0` at callback slot `+0x158`. Its effective
capture ABI is:

```c
int read_image(uint16_t *output, bool tx_enable, bool hv_enable,
               uint16_t *dac, bool adjust_dac, bool is_finger,
               uint8_t capture_mode);
```

The two image calls use:

```text
0x180015d69..0x180015d91:
  output=first temporary, tx=1, hv=1, dac=&context[0x310],
  adjust=0, finger=0, mode=0

0x180015e31..0x180015e4f:
  output=second temporary, tx=0, hv=1, dac=&context[0x310],
  adjust=0, finger=0, mode=0
```

- Dimensions are context bytes `+0x1f0` (rows, `88`) and `+0x1f1` (columns,
  `108`).
- Image size is `rows * columns * 2` (`0x4a40`).
- Pair threshold is context word `+0x314`; profile setup writes `200`.
- `FUN_180014ce8` receives rows, columns, first/TX-on image, second/TX-off
  image, and threshold in that order. It is read-only and computes the interior
  mean absolute difference.
- The TX-off image is validation input only. It is never averaged into or
  retained as the image reference.

## Ownership

- The first temporary image is allocated at `0x180015d03..0x180015d0b`; the
  second is allocated at `0x180015d17..0x180015d1f`.
- HAL initialization allocates persistent image storage `+0x248` with
  `rows * columns * 2` bytes.
- Complete admission copies only the first/TX-on temporary into `+0x248` at
  `0x180015f45..0x180015f52`.
- Both temporary images are freed at `0x18001601f..0x18001602f` on every path.
- HAL shutdown owns and frees the persistent `+0x248` allocation.

## Common Postlude And Validation Rejection

The image-pair predicate controls whether the second TX-on FDT read occurs. Both
image-pair rejection and rejection of that final TX-on FDT comparison bypass
the successful image/validity block at `0x180015f45..0x180015fc9` and enter the
common postlude at `0x180015fce`:

1. Callback `+0x88` (`FUN_1800048d0`) transforms the first TX-on FDT sample in
   place with `(sample & 0xfffe) * 0x80 + (sample >> 1)`.
2. If the transform succeeds, the transformed 24 bytes replace retained
   FDT-calibration storage `+0x268` at `0x180015fe6..0x180015ff9`.
3. `FUN_18000da18` persists the acquired set only when `+0x232 == 1`.
4. Callback `+0x68` (`FUN_180005950`) receives the transformed FDT base at
   `0x18001600f..0x180016017` and copies it to the profile-9 primary, down-arm,
   up-arm, and manual-FDT base stores.

Either validation rejection leaves `+0x248` and `+0x237` unchanged. Their prior
state may represent an old valid image, or no valid image when the initial
acquisition started with `+0x237 == 0`. The function does not clear `+0x232` on
rejection.
On the documented profile-9 initial and refresh routes, initialization or the
caller enters with `+0x232` clear; rejection does not set it, so persistence is
skipped on those routes. Rejection also does not write one-shot byte `+0x236`.

The validation predicates have no direct error status. The common postlude's
`+0x88` and `+0x68` callbacks own the final return value, so either rejection can
return zero when those callbacks succeed. This function does not arm FDT-down
or FDT-up detection and invokes no completed-frame callback on either path.

The exact FDT transform and its command consumers are documented in
`usbinterface-FUN_1800048d0.md`.

## Callers And Lifetime

- `FUN_1800162ac` installs this function at HAL callback slot `+0x180`.
- Full initialization dispatches slot `+0x180` through device action `0x0c` at
  `FUN_180020970:0x180020d04`.
- `FUN_1800141f0` calls it at `0x180014257` when `+0x232` is clear.
- `MilanHV_temperature_event` (`FUN_180013da4`) clears `+0x232`, calls it at
  `0x180013e01`, and sets `+0x236` only if the call restores `+0x232`.
- `Reverse_Occure` (`FUN_180014480`) calls it at `0x1800144cd` when `+0x232`
  is not one.
- Normal probes reuse `+0x248`; this function is not called per probe.
- D0 exit/entry does not free `+0x248`. Full HAL shutdown frees it.

## Handoff

- Capture completion loads retained image reference `+0x248` as callback
  argument 2 at `0x18000e63d..0x18000e652`.
- `CaptureFramedone` (`0x18001fb40`) serializes that argument as the sample's
  setup/reference plane.
- `GoodixEngineAdapter.dll` copies that plane from payload `+0xebf0` and passes
  it through `_InitPreProcessor_Unify` to `preprocessor_init`.
