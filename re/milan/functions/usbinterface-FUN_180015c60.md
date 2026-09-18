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
The selected template, conditional calibration writes, and complete checksum
contract are documented in `usbinterface-FUN_180005094.md`.
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
- The image validator excludes two rows/columns on each border: rows
  `2..rows-3`, columns `2..columns-3`. It sums absolute differences of
  unsigned 16-bit samples in a 64-bit accumulator, divides by
  `(rows-4)*(columns-4)` with integer truncation, and admits only when that
  quotient is **strictly less** than the threshold. For profile 9 this is
  `floor(sum / 8736) < 200`; a quotient of 200 rejects. The FDT validator's
  per-area comparison separately admits equality to `+0x31c`.
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

For a single-cell command, `FUN_180018a8c` writes the selector, two-byte length,
payload and checksum into its stack cell, then passes all 64 bytes to
`FUN_18001c73c`. It does not initialize the suffix after the checksum. The image
request defines eight protocol bytes; manual-FDT and arm requests define thirty.
The remaining transferred bytes are outside the declared protocol message.

### Independent Image Reception

`DataFromDevice` (`FUN_18001a7ec`) assembles 64-byte protocol cells and checks
the declared message checksum before dispatching category 2 through HAL callback
`+0x140`. It does not require an active image sender, an image ACK, or command
selector zero for that category dispatch. Profile 9 installs `FUN_1800048a0`,
which calls `FUN_180007968` with the assembled payload and its length excluding
the protocol checksum.

The raw callback performs authenticated reply processing, CRC verification and
raw12 conversion before signalling selector 8 through `FUN_18000dd84`. The
selector maps to HAL event `+0x2c8`. Successful conversion writes the persistent
decoded-frame buffer `DAT_180060708`; the signal precedes publication of zero
to raw-status global `DAT_180060cc0`. The reader callback completes those stores
before returning. Authentication/CRC failure does not signal image readiness.
CRC failure writes raw status `-1`; reply-processing failure writes the callback's
local status `-1` without updating that global. Neither failure converts a frame.
See `usbinterface-FUN_180024940.md` for authenticated counter mutation and
`usbinterface-FUN_180009a64.md` for complete-frame conversion.

The sender resets only the event before writing. An image that is fully received
while the sender polls ACK can therefore advance the authenticated receive
counter and replace the decoded buffer before ACK succeeds or times out. ACK
success subsequently consumes the already-signalled event; ACK exhaustion skips
the event wait and leaves the decoded buffer and counter mutations intact.
The next image request resets readiness again, rather than reusing that signal.

Protocol assembly belongs to the continuous receiver, independently of those
command deadlines. `FUN_18001a7ec` retains the declared length at
`DAT_180084120`, copied-byte count at `+4`, cell count at `+8`, and active-partial
byte at `DAT_180084130` between calls. Neither the image sender's ACK/response
timeout nor its next command write clears this assembly. An incoming cell with
a different `cell[0] >> 1` clears partial-active; an even first-cell selector
starts a new message. An odd continuation is ignored when partial-active is
not one, otherwise it appends to the retained message. Thus an unfinished image
can finish during a later command's ACK wait if its remaining cells arrive
before a different-selector packet. A later ACK received first instead
interrupts that partial image through the receiver's selector rule.

The profile-9 down procedure can therefore return image failure and issue its
down-arm command while an authenticated image is still incomplete. Completing
that image before the down-arm ACK advances the receive counter, replaces the
decoded cache and signals selector 8 without publishing the failed acquisition.
The following manual-FDT request leaves that image state intact; the following
image request clears only its readiness before acquiring another frame. These
transitions do not replace the retained image reference at HAL `+0x248`.

If another image is received before ACK satisfaction, the single readiness
event and raw-status store have distinct ownership. A successful first image
followed by an authenticated CRC failure leaves readiness signalled and the
first decoded frame intact, but raw status `-1` makes the image callback fail
after ACK. Both authenticated replies have advanced the receive counter. An
unauthenticated second reply leaves the first frame, its zero raw status and
the signal intact, so the sender can still publish the first image after ACK.

The authenticated counter increments modulo `2^32`, including for CRC failure:
`0xffffffff` becomes zero. A later authentication failure preserves both that
wrapped counter and an already-failed raw status. A new request does not clear
the failed status or decoded cache; a later successfully authenticated and
converted image replaces the cache and restores raw status zero. This is the
raw callback's state contract; the continuous reader's separate error-history
and GTLS-restart policy is owned by `usbinterface-FUN_180025400.md`.

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

### Acquisition Read Failures

A null context returns `-1` without allocation or transport. Failure of either
temporary allocation returns zero after freeing any first allocation; it does
not invoke mode 4 or an acquisition callback.

A mode-4 return of `-1` at `0x180015d42..0x180015d45`, or failure of the first
TX-on FDT read at `0x180015d58..0x180015d63`, branches directly to temporary
buffer cleanup. These exits do not run the common FDT postlude, replace the
retained image or FDT buffers, or change `+0x232/+0x237`.

Once the first TX-on FDT read has succeeded, a `-1` from the first TX-on image,
TX-off FDT, second TX-off image, or final TX-on FDT acquisition instead reaches
the common postlude at `0x180015fce`. These failures do not replace the retained
image bytes and do not change `+0x232/+0x237`. Callback `+0x88` then determines
whether the first TX-on FDT sample is transformed. If it returns `-1`, the
function returns `-1` without replacing `+0x268` or programming the FDT stores.
If it succeeds, the transformed sample replaces `+0x268`, callback `+0x68`
programs the FDT stores, and that callback's result becomes the function result;
the earlier image or FDT read error is not preserved as the return value.

For the installed profile-9 callbacks, both postlude calls receive the address
of the owner's non-null 24-byte stack sample. `FUN_1800048d0` and
`FUN_180005950` return `-1` only for a null pointer; their non-null paths perform
the transform and four-store copy and return zero. Consequently, after the
first TX-on FDT succeeds, an ordinary acquisition failure or validation rejection
returns zero through this concrete profile's postlude. A transform/setter error
is not an additional ordinary transport-failure stage. The only acquisition
callback failures preserved as `-1` are mode 4 and the first TX-on FDT read.

The retained flags are independent: the admitted block writes both
`+0x232/+0x233` with the single word store `0x0101` and sets `+0x237 = 1`.
All acquisition-failure exits preserve `+0x233`, `+0x236`, and `+0x237`, as well
as `+0x232`'s entry value; temperature/event callers separately clear `+0x232`
before entry. No rejected or failed candidate image replaces `+0x248`.
Decoded reception storage `DAT_180060708` is a separate owner: already completed
image callbacks may have replaced it even when the base attempt later fails.

Persistence runs after image/validity publication and after `+0x268` replacement.
`FUN_18000da18` returns void; allocation failure exits that helper, and its file
write result is unused. Neither outcome rolls back the admitted image or flags
or prevents the subsequent `+0x68` FDT-store publication.

The profile-9 initial, temperature, reverse-invalid, and up-invalid callers enter
these paths with `+0x232` clear. A failed attempt therefore leaves reacquisition
to a later qualifying down/up/reverse event; ordinary operation start does not
retry slot `+0x180` merely because validity remains clear.

The function contains no `Sleep`, mode-2 request, or `EcControl` call.
Allocation, configuration, acquisition, validation, transform, persistence, and
FDT-store outcomes all return without a power transition; the full-initialization
worker or event caller owns the continuation.

## Callers And Lifetime

This owner does not clear software drift-anchor words `+0x320..+0x337` or
anchor-empty byte `+0x338`, including on complete admission. Its admitted
publication at `0x180015f45..0x180015fc9` replaces the retained image and sets
validity; the common postlude replaces retained/programmed FDT bases, not the
software anchor. Persistence `FUN_18000da18` serializes the acquired buffers
without modifying the anchor. The profile-9 base setter `FUN_180005950` writes
the primary/down/up/manual global base stores only.

Consequently `Milan_checkbase_isok` recovery through `UP_Occure` preserves an
active anchor that survived the up handler's majority and proximity tests.
Reverse and majority-up callers separately clear the anchor after successful
acquisition; their caller-owned clears must not be attributed to this callee.
See `usbinterface-FUN_1800149c4.md`.

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
- The enabled-HAL saved-base repair route can copy a file image to `+0x248`
  without invoking this owner or changing its validity/marker bytes. Cold
  saved-base loading instead precedes this owner's fresh acquisition with
  validity still clear. Both contexts of `FUN_18000d24c` are described in
  [the initialization owner](usbinterface-FUN_180020970.md).

## Handoff

- Capture completion loads retained image reference `+0x248` as callback
  argument 2 at `0x18000e63d..0x18000e652`.
- `CaptureFramedone` (`0x18001fb40`) serializes that argument as the sample's
  setup/reference plane.
- `GoodixEngineAdapter.dll` copies that plane from payload `+0xebf0` and, when
  the setup gate below requests initialization, passes it through
  `_InitPreProcessor_Unify` to `preprocessor_init`.

Hardware image replacement is distinct from engine setup replacement. This
function never sets one-shot byte `+0x236`; direct invalid-base callers therefore
can replace `+0x248` while leaving an already-clear marker clear. For example,
temperature-event validation rejection preserves an older image-valid reference
but leaves FDT-base validity clear. A later non-majority up event can recover
through `Milan_checkbase_isok -> MilanHV_update_allbase` without writing the
marker. `EngineAdapterAcceptSampleData` (`FUN_18001f610`) then skips its setup
bridge when engine-context `+0x7c` is already nonzero: it uses the existing
preprocessing workspace despite the newly supplied hardware reference. The
engine gate is `initialized == 0 || marker == 1`, not reference-byte inequality
or hardware acquisition success. An earlier unconsumed marker of one remains
one through this direct recovery and still requests setup using the latest
reference at the next delivered sample.

## Current Source Map

- `usbinterface.dll:0x180015c60` maps to
  `drivers/goodix53x5/device/base.c:goodix_base_ssm_handler`, the
  `goodix_milan_base_attempt_*` helpers, and `goodix_base_complete_recovery`.
  Initial configuration is split into
  `device/session.c:goodix_open_ssm_handler`; forced configuration is in the
  base state machine.
- `0x180014c98` and `0x180014ce8` map to
  `device/calibration.c:goodix_device_is_fdt_base_valid` and
  `device/base.c:goodix_milan_base_pair_mad`/`goodix_milan_base_attempt_admit`.
- HAL image reference `+0x248` maps to the separately owned
  `FpiDeviceGoodix53x5.hardware_reference`, copied by `goodix_base_ssm_handler`
  after both FDT checks and image-pair admission, including unmarked recovery.
  `device/calibration.c:goodix_device_adjust_dac` consumes this latest plane;
  rejected attempts retain it. See `usbinterface-FUN_180007c84.md`.
- The hardware reference also supplies the engine's setup input. Its consumed
  setup/workspace lifetime maps to `GoodixMilanGeneration`,
  `goodix_milan_generation_prepare_setup`,
  `goodix_milan_generation_transfer_process_state`, and the unmarked direct
  recovery branch in `goodix_base_ssm_handler`.
- The independent decoded-image cache maps to the receive owner in
  `device/transport.c` and `device/commands.c:goodix_cmd_dup_image_reply`.
  Down/up/manual FDT stores map to `GoodixProfile9FdtState.base_*`; caller-owned
  post-refresh down restoration is described in `usbinterface-FUN_180014480.md`.
