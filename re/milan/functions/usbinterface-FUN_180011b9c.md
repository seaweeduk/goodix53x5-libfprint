# usbinterface.dll FUN_180011b9c

## Identity And Profile-9 Entry

- Address: `0x180011b9c`; logged name: `gf_broken_check`.
- Role: acquire a sensor-health image pair, update broken-pixel history, and
  publish next-enrollment/study permission bytes.
- Profile-9 caller: `FUN_180015aa0` (`MilanHV_UP_procedure`) at `0x180015b2a`,
  after its ordinary up/base procedure, only when HAL `+0x2e1 != 0` and
  image-valid `+0x237 == 1`. Arguments are HAL `+0x30c`, zero, and operation
  dword `+0x308`. The middle argument is unused by this owner.
- The [up wrapper](usbinterface-FUN_180015aa0.md) does not require a request or
  callback. Its health result cannot request or reject a new reference: the
  ordinary up/reference decision has already run, and health always rejoins
  down-arm.
- Current sensor-health owners are in `drivers/goodix53x5/device/health.c`;
  image classification's broken-pixel masks are separate from this hardware
  health contract. See the native/current mapping below.

`GxFNHV_MilanOpen` (`0x18000450c:0x180004665..0x18000470b`) enables this
owner only for configuration byte `+0x424 == 1`. Enabled initialization sets
HAL `+0x2e4=200`, `+0x2e8=600`, `+0x2ea=300`, `+0x2e0=0`,
`+0x30d=1`, and `+0x30e=0`. Disabled initialization clears the three
threshold words and `+0x2e1`, sets `+0x30d=1` and `+0x30e=1`, and does not
write `+0x2e0`. Thus boot health initialization permits the first enrollment
sample independently of the boot count.

The enable source is the global configuration returned by `FUN_180009c50(0)`,
not an OTP field or operation selector. Its compiled byte `0x18005e764` is
one; the registry loader's `SensorBrokenCheckSwitch` first-byte override and
driver-entry ordering are described in
[all-base configuration](usbinterface-FUN_180015c60.md#optional-sensor-check).
There is no per-profile numeric default different from this selected value.

`OnCaptureData` (`0x180021978`) supplies `device_get_data` (`0x18000ebcc`)
with count two and operation zero for purpose four (enrollment), otherwise
count one and operation one for its accepted standard purposes. The latter
stores count at HAL `+0x280` and operation at `+0x308`; neither callback
consumption nor deactivation clears the operation. The static HAL's fresh
operation is zero. Thus request-free UP can use either enrollment or
non-enrollment health policy, according to the last admitted capture selector.
The HAL constructor clears unread count `+0x280`, but neither it nor HAL
destruction `FUN_1800160a0` clears operation `+0x308`. Health initialization
resets its per-HAL workspace and permissions, not this retained selector.

## All-Base Producer And Pair Ownership

The enabled all-base producer is described in
[the all-base contract](usbinterface-FUN_180015c60.md#optional-sensor-check).
Every `FUN_180015c60` invocation reaching successful TX-off manual FDT read
attempts the health pair before the first FDT comparison. This is not limited
to startup or `+0x2e0 == 0`. The boot flag gates only later history seeding,
after complete reference admission. An UP that invokes all-base refresh can
therefore take a health pair inside that refresh and another in its wrapper:
the wrapper independently tests the resulting retained image-valid byte.
`FUN_180012d38` captures two TX-on/HV/no-finger images into global workspace
`0x1800615e8` and its `+30000` span, using modes 2/3 and DAC values HAL
`+0x2f6` / `uint16(+0x2f6 - +0x2fe)`. These are separate from retained base
images. Profile 9 adds no sleep in this helper. A first result of `-1` skips
the second capture. Workspace dword `+60000` becomes one after the first
non-`-1` capture and two after the second non-`-1` capture. This helper does
not reset that dword on entry and returns the final capture result.
Both calls disable adjustment and finger-image mode. They do not publish an
engine generation, live sample, reference image, reference-validity flag, FDT
base, or refresh marker. The low-DAC source maps to `calib.dac_l` and its
delta to `calib.dac_delta`; it is not live/current high DAC `calib.dac_h`.
Profile-9 `FUN_1800055d0 -> FUN_1800074bc` does not consume the mode argument
2/3 in its wire construction: both requests use category two, command zero,
TX-on opcode `0x01`, configured HV byte, and the selected little-endian DAC
word. The existing image-command primitive can express both acquisitions.

After complete base admission, preparation result zero and HAL `+0x2e0 == 0`,
`FUN_180012c1c` computes a count through `FUN_180013578`, fills all thirty
history words at `[0x1800615d8]+8` with its low word, stores that word in
`0x1800615d0`, and calls record writer `FUN_180013130`. Its write result is
ignored. The base owner then sets `+0x2e0=1`. Boot measurement does not write
the two permission bytes or reject an otherwise admitted base.

`FUN_1800121a4` zeroes the thirty-word history and pair workspace at HAL
construction, then attempts the existing history-file load. A missing/invalid
record leaves the zero history. A valid record fills all thirty words but does
not set global prior estimate `0x1800615d0`. Successful boot measurement
overwrites both regardless of the loaded record. The global estimate and
enrollment count `0x1800615d4` are not reset by health initialization or
`FUN_1800120b4` destruction; destruction releases/nulls the context, record,
pair workspace and event. The enrollment count's explicit reset owner is
`FUN_180013454`, called on deactivation. A failed boot pair can therefore leave
zero/loaded history alongside a surviving estimate within the same module.

## Health Predicates And Failure

A null status pointer returns one. Operation zero increments the wrapping
16-bit enrollment-check count `0x1800615d4` before acquisition and selects
unsigned threshold HAL `+0x2e8`; other operations select HAL `+0x2ea`.
Any nonzero preparation result returns immediately, retaining both permission
bytes and history (the operation-zero counter increment has already occurred).

On success define:

- `c = uint16(FUN_180013578(pair))`;
- `h = uint16(FUN_1800138bc(c, history, &0x1800615d0))`;
- `d = abs(int32(c) - int32(h))` (tested as an unsigned 16-bit value).

`FUN_1800138bc` chooses its estimate from the previous thirty-word history,
then inserts `c` through `FUN_180013b1c` and stores `h` into `0x1800615d0`
before returning. Consequently the following enrollment statistic sees the
updated history.

For operation zero, `m` is the signed low word returned by `FUN_1800137cc`
on the first `min(uint16(enrollment_check_count), 4)` history entries:

```text
status[1] = h < enroll_threshold && (c < enroll_threshold || m > 10)
status[2] is retained
```

The unsigned count comparisons and signed strict `m > 10` branches are at
`0x180011e82..0x180011ea5`; the failure store is `0x180011ef9`.
For every nonzero operation:

```text
status[2] = c < study_threshold && h < study_threshold && d < 20
status[1] = c < enroll_threshold
```

These unsigned strict comparisons/stores are at
`0x180011f25..0x180011f8c` and `0x180011fcf..0x18001200e`.
The final persistence branch tests `h != word[0x1800615d0] && d < 20`
at `0x180012051..0x180012071`; the lookup has already assigned that word.
The ordinary synchronous lookup-return path therefore skips this write.
The owner returns zero after publishing either healthy or unhealthy status.

### Count Producer

`FUN_180013578` uses dimensions copied from HAL `+0x1f0/+0x1f1` by
`FUN_1800121a4`. Its inset word at `0x18005e800` is one. For 108-by-88
profile-9 images this leaves `106*86=9116` interior pixels. Each pairwise
difference is formed/subsequently negated in 16 bits using a signed-positive
test; for decoded 12-bit frames this equals the ordinary absolute difference.
The first pass accumulates only differences in the inclusive interval
`801..1199` (`0x18001360b..0x180013626`). With no such pixels it returns the
full interior area. Otherwise the unsigned integer mean of those selected
differences defines low/high words `mean-200` and `mean+200`; the second pass
counts differences strictly outside that inclusive interval.

Consequently a constant-difference-1000 pair produces zero broken pixels;
an equal-image pair produces 9116. Boot fills the history with the produced
count. The next lookup on an equal-valued history selects its first value
before inserting the new count, so neither count family depends on
uninitialized history or a later estimation branch.

### History Estimate And Insertion

`FUN_1800138bc` evaluates the old history before inserting the new count:

1. If its first four words are equal, use the first word.
2. Otherwise, if the signed low word of `FUN_1800137cc` on the first eight
   words is less than five and is not `-1`, use that helper's rounded mean.
3. Otherwise, for each of thirty physical history indices count and sum all
   history values with absolute difference less than five from that index's
   value. Select the first index with the greatest count (strict-greater
   replacement). Its candidate is integer `sum/count`. Use that candidate only
   when the cluster contains at least ten values and the prior estimate is
   at least `candidate + 600`; otherwise retain the prior estimate.

`FUN_1800137cc` computes the rounded unsigned-word mean, then the mean of
the squared integer distances from that rounded mean. Squaring is a low-32-bit
multiply interpreted as signed before conversion to double; measurements and
their native randomized replacements are too small to overflow that product.
It obtains the square root by Newton iteration, starting from the variance,
stopping when successive values differ by at most `FLT_MIN`, and returns the
root rounded by adding 0.5 before integer conversion. Its name/log calls this
a mean-square error, but the returned quantity is the rounded root. The
zero-length path executes floating-point division by zero; with ordinary masked
x86 exceptions the mean
conversion produces `0x80000000` (stored low word zero). The root loop executes
one NaN iteration; the unordered comparison then exits and conversion produces
`0x80000000`. Its consumed signed low word is zero. The outer wrapping
enrollment counter selects this path when it wraps to zero.

Insertion (`FUN_180013b1c`) shifts old entries 0..28 to 1..29 and writes the
new value at index zero. Before insertion, a count strictly greater than
`uint16(int(interior_area * 0.9))` is replaced by
`601 + random_u16 % (pixel_limit - 600)`. For profile 9 the limit is 8204,
the divisor is 7604, and replacements are 601..8204. The two-byte random
destination is initialized to zero and the random helper's return is ignored.
This replacement affects history only: the current count used by permission
predicates remains the measured count. Finally lookup stores its selected
estimate through the prior-estimate pointer before returning it.

### Logging And Completion State

Health logs entry/operation, prior/measured/output counts, and (for `d < 20`)
the real-time count. Enrollment logs count, selected length and rounded root;
its healthy path logs at level seven and disable path at level four.
Non-enrollment logs healthy study at level seven or disabled study at level
four, and logs both enable/disable-next-enroll messages at level four. These
logs do not add hardware actions. History insertion dumps all sixty bytes at
level eight.

Enabled down handling (`FUN_180014e10`) clears study permission and resets
event `+0x300` before manual validation, including a false-down or failed
manual attempt. UP signals the event after its eligible health attempt,
including acquisition failure. `FUN_180013454` waits at most 1500 ms when
enabled, logs `not up` on any nonzero wait result, signals the event again,
and resets the wrapping enrollment count to zero. It always returns zero;
it neither clears history nor changes either permission byte. Initial event
creation is auto-reset and immediately signaled.

## Publication Consumers

The up wrapper ignores a nonzero health-check return, signals HAL event
`+0x300`, and continues ordinary FDT-down rearming. A health failure is not
an up-handler failure.
The eligibility byte is image-valid `+0x237`, not FDT-base-valid `+0x232`:
temperature refresh clears the latter while preserving the former. Consequently
a rejected reference refresh can still be followed by ordinary UP health.

`CaptureFramedone` (`0x18001fb40`) copies HAL `+0x30d` to image payload byte
`+2` when configuration byte `+0x424 == 1`; otherwise it writes one. See
[sample publication](usbinterface-FUN_18001fb40.md).
This copy occurs during live callback publication, before that cycle's later
UP health. It is an immutable permission input to that sample; a later health
update affects a later callback rather than retroactively changing it.
`OnActivate` (`0x1800214d0`) deactivation calls `FUN_180013454` and returns
HAL `+0x30e` as its one-byte output, then requests mode-2 sleep. The helper
waits on `+0x300` when health checking is enabled and clears the enrollment
check count; see
[deactivation](usbinterface-profile9-fdt-event-loop.md#request-cancellation-and-deactivation).

In `GoodixEngineAdapter.dll`, `EngineAdapterAcceptSampleData`
(`0x18001f610`) reads payload byte `+2` when its own configuration byte
`+0x424 != 0`, otherwise substitutes one
(`0x18001fb9b..0x18001fbde`). After reference setup, purpose 4 with that
value zero returns `0x80098008` and reject detail 10 at
`0x18001ffe2..0x180020040`, skipping live preprocessing and extraction.
Other purposes do not take this gate. The stack-local enrollment value is
separate from engine byte `+0x1b7` and survives the later calibration copies.
The current mapping is the admission portion of
`drivers/goodix53x5/milan/runtime.c:goodix_milan_runtime_preprocess_input`,
called by `goodix_milan_runtime_run`. Production inputs bind the permission
snapshot taken at completed capture publication. The enrollment gate follows
successful reference setup and its setup-save hook, but precedes live
preprocessing and quality/coverage checks. Offline callers without a hardware
permission snapshot retain the default-allowed path.

The reported study byte has a different consumer boundary. Successful
one-byte deactivation I/O in engine `FUN_18001e230` replaces `+0x1b7` and
logs it. `EngineAdapterDeactivate` (`0x180023610`) then calls
`FUN_180031510` regardless of I/O result. That worker does not test `+0x1b7`
or pass it to its update wrappers: its arguments are the match byte,
template size, template destination, and action output. The health byte
therefore does not gate this deactivation study/storage path. See
[engine deactivation](FUN_180023610.md) and [update publication](FUN_180031510.md).

## Native / Current Mapping

`drivers/goodix53x5/device/health.c` owns these bounded counterparts:

| Native owner | Current function / state |
| --- | --- |
| `013578`, pair count | `goodix_health_measure` |
| `0137cc`, rounded statistic | `goodix_health_statistic`, including wrapping-count zero length |
| `0138bc`, old-history estimate | `goodix_health_lookup` |
| `013b1c`, randomized history insertion | `goodix_health_insert` |
| `011b9c`, permissions | `goodix_health_evaluate`; UP child increments the module counter before acquisition |
| `012d38`, pair acquisition | `goodix_health_start_pair` and its serialized child; caller selects BASE or UP |
| `00450c/0121a4`, enabled initialization | `goodix_health_reset`; zero history models the missing-file branch |
| `012c1c`, admitted boot measurement | `goodix_health_seed_base`, consuming a copied successful BASE measurement after reference admission |
| `014e10`, down health stores | `goodix_health_note_down` |
| `00ebcc`, retained operation | `goodix_health_note_capture`, called at new foreground capture/stage admission, not partial-read retry |
| `013454`, deactivation state | `goodix_health_is_complete` and `goodix_health_finish_deactivation`; coordinator owns the single 1500-ms wait |
| `01fb40`, sample permission | `goodix_health_enroll_allowed`, sampled at live publication |

`GoodixSensorHealth` is embedded in the device. Pair frames belong to the child
and are freed after its completion callback; `GoodixHealthMeasurement` contains
only an availability flag and count, so BASE can copy it until validation.
Neither BASE completion nor UP evaluation publishes a reference or adjusts DAC.
Module-static estimate/count are protected by a mutex and survive cold reset;
reset retains the per-owner operation selector while resetting history,
boot-seeded, permissions and completion. There is no health disk I/O.
This maps the compiled enabled configuration and missing-history-file branch,
not the Windows registry-disabled or present-history-file branches. In particular,
a failed boot pair does not import native persisted history. The operation
selector survives reconstruction of the same Linux device owner; creating a
new `FpDevice` supplies zero rather than inheriting the static native HAL's
last operation selector.

The child's ordinary exhausted read returns an unavailable measurement without
a terminal error. Ordinary UP failure still signals completion, preserving
permissions/history and its already-incremented enrollment count. Ownership
rejection, physical-reader failure and cancellation remain terminal host results.
The caller serializes the child with hardware work and joins it before owner
reset/destruction. Reference/UP call sites and runtime sample admission are
separate consumers of these module APIs.

`device/base.c:GOODIX_BASE_FDT_TX_OFF_DONE` starts BASE before the first FDT
comparison; complete reference admission alone calls `goodix_health_seed_base`.
`device/scan.c:GOODIX_SCAN_COORD_UP_HEALTH` starts UP only for the selected UP
event with a retained `hardware_reference`, after its reference continuation
and before down-arm. An UP that refreshes can execute both pairs. Deactivation's
`goodix_scan_start_health_wait` lends ownership to a maintenance child while its
single timer runs, then joins selected work before sleep; 1500 ms is not a
strict total-cleanup bound. The complete join and shared-slot EC mapping is in
[the event loop](usbinterface-profile9-fdt-event-loop.md#current-source-mapping).
