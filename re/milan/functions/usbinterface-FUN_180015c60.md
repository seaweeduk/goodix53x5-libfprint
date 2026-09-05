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

Entry does not test persisted-base byte `+0x231`, base-valid byte `+0x232`,
or image-valid byte `+0x237` to skip acquisition. With a non-null context and
both temporary allocations successful, mode 4 is attempted regardless of
those bytes; only its successful return permits the first TX-on FDT read.
The first and final TX-on FDT
readings are separate sensor commands; the final reading is not a copy of
the first.

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

### Optional Sensor Check

After the TX-off FDT read, before its comparison, nonzero context byte
`+0x2e1` adds `FUN_180012d38` (`gf_broken_check_prepare_image`).
`FUN_18000450c` sets this byte exactly when configuration byte `+0x424`
returned by `FUN_180009c50(0)` equals one.
That accessor returns the configuration object at `0x18005e340` without
reloading it when its argument is zero. The compiled byte at `0x18005e764`
is one; this is the stored default, not a guarantee that configuration loading
leaves the runtime value unchanged.

`FUN_180009c6c` loads `SensorBrokenCheckSwitch` from
`HKLM\Software\Goodix\FP`. A successful value read supplies its first byte
to `0x18005e764`; a missing key or missing value leaves the existing byte
unchanged. `FUN_180009c50` invokes this loader only for a nonzero argument.
The profile-open consumer uses exact equality to one, not general nonzero
configuration truthiness, when publishing HAL byte `+0x2e1`.
`DriverEntry` (`FUN_18002409c`) calls `FUN_18001dd34`, which calls
`FUN_180009c50(1)`, before creating the framework driver. Thus configuration
loading precedes device creation and the profile-open callback. With no
registry override in a fresh process the compiled one survives this load;
an override whose first byte differs from one selects the disabled route.

After publishing that byte, `FUN_1800162ac` calls `FUN_1800121a4`
(`gf_broken_check_init`) when it is nonzero. With successful allocations,
this initializes a 0x44-byte context at `DAT_1800615d8`, a 0x58-byte history
record at `DAT_1800615e0`, and a 0xea64-byte image workspace at
`DAT_1800615e8`. It zeroes all three, stores the HAL pointer in the first
context word, copies sensor dimensions, creates/signals the HAL `+0x300`
event, and calls `FUN_180012498` to load the history array. The history-load
return is not propagated: absent history does not disable the initialized
workspace. The HAL initialization caller also does not inspect this helper's
return. Allocation failure is not an alternate valid workspace producer.

For profile 9 with an initialized sensor-check context, `FUN_180012d38`
captures two additional TX-on, HV-enabled, no-finger images through `+0x158`,
with adjustment disabled. The first uses DAC word `+0x2f6` and mode 2;
if it does not return `-1`, the second uses `+0x2f6 - +0x2fe` and mode 3.
Their destinations are the sensor-check workspace and that workspace plus
30000 bytes, not the two base-image temporaries. This helper adds no fixed
sleep on its profile-9 branch.

Following complete base admission, if `+0x2e1 != 0`, preparation returned
zero, and `+0x2e0 == 0`, the owner calls `FUN_180012c1c`
(`gf_broken_check_on_bootup`) and sets `+0x2e0` to one. Thus the five primary
acquisitions describe the sensor-check-disabled route; enabling this option
adds sensor-check acquisition and processing, not replacement base samples.

The boot helper computes the broken-pixel count with `FUN_180013578`, fills
the 30-word history array at sensor-check context `+8` with its low 16 bits,
stores those bits in `DAT_1800615d0`, and calls `FUN_180013130`. The latter
writes a 0x58-byte `broken_check_record.dat` containing the count/complement,
sensor identity, version, and checksum. Its return is ignored by the boot
helper, which returns zero; `update_allbase` likewise does not use the boot
helper's return to change its base-admission result.

This count is not solely a log value. The later profile-9 up procedure
`FUN_180015aa0` calls `FUN_180011b9c` with HAL status storage `+0x30c`
when `+0x2e1 != 0` and image-valid `+0x237 == 1`. That helper acquires a
new sensor-check pair and passes the new count, the history array, and
`&DAT_1800615d0` to `FUN_1800138bc`. Its resulting health assessment writes
the next-enrollment and study status bytes at HAL `+0x30d/+0x30e`.
The up procedure does not propagate a nonzero sensor-check return as its
own failure; it signals `+0x300` and continues to FDT-down rearming.
These are later health-state consumers, not boot-time OPEN rejection:
`FUN_180012c1c` itself does not write those two status bytes or gate OPEN
completion on the measured count.

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

### Image Command Producer

`FUN_18000450c` installs `FUN_1800055d0` at slot `+0x158` and
`FUN_180005420` at slot `+0x160`. The image callback calls
`FUN_1800074bc`, which issues one category-2, command-0 request through
`FUN_180017ec0` with a 500 ms response timeout. The four payload bytes are:

- Byte 0: `0x01` for TX enabled or `0x81` for TX disabled, with `0x40` added
  only for a finger image. Both base calls use no-finger mode.
- Byte 1: the configured HV byte from configuration offset `+0x426` when HV
  is enabled, or `0x10` when disabled.
- Bytes 2 and 3: the little-endian DAC word supplied by the caller.

Command failure or raw-data status `DAT_180060cc0 == -1` sets the image
callback result to `-1`. On success, the producer copies the decoded frame
from `DAT_180060708` to the caller's image buffer, then invokes
`FUN_180007c84` with the adjustment flag and DAC pointer. The base calls pass
adjustment flag zero. There is no second image command in this producer.
`FUN_180007c84` returns without reading the image or changing the DAC when
that flag is zero.

### Command Waits

The image branch of `FUN_180019ec8` (`ChangeMode`) calls `FUN_180018dd8`
once with ACK timeout 500 ms, response timeout 500 ms, and response-event
selector 8. The configuration producer uses selector 1 with the same timeouts
and retries only a failed attempt. Manual FDT uses selector 10 and likewise
retries only failure; see `usbinterface-FUN_180005420.md`.

`FUN_180018dd8` resets the selected response event before sending and, after
successful send/ACK handling, waits in 50 ms `WaitForSingleObject` calls until
a result other than `WAIT_TIMEOUT` or expiration of the response timeout.
These are interruptible response waits, not fixed inter-acquisition delays.
`MilanHV_update_allbase` itself inserts no sleep between the five acquisitions.

Before the response-event wait, `FUN_180018a8c` sends the framed command and
polls its ACK byte in `DAT_180068920`, indexed by
`(category * 8 + command) * 0x18`. It returns immediately when bit zero is
set. Only an unset ACK causes `timeBeginPeriod(1)`, `Sleep(1)`, and
`timeEndPeriod(1)` before the next poll, bounded by the supplied ACK count.
This is ACK polling, not a mandatory settling delay after a received ACK.

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

The FDT validator `FUN_180014c98` (`0x180014c98..0x180014ce6`) reads
12 unsigned words from each sample, shifts each operand right by one before
subtraction, and rejects as soon as
`abs((first[i] >> 1) - (second[i] >> 1)) > context_word_31c`.
Equality is admitted; it is not a difference-then-shift comparison. The helper
is read-only and returns a Boolean in `AL`. The first comparison calls it at
`0x180015de2`; a false result branches through `0x180015fcb` to the common
postlude without capturing the TX-off image or another TX-on FDT sample.

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
