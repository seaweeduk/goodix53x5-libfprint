# usbinterface.dll FUN_180011b9c

## Identity And Profile-9 Entry

- Address: `0x180011b9c`; logged name: `gf_broken_check`.
- Role: acquire a sensor-health image pair, update broken-pixel history, and
  publish next-enrollment/study permission bytes.
- Profile-9 caller: `FUN_180015aa0` (`MilanHV_UP_procedure`) at `0x180015b2a`,
  after its ordinary up/base procedure, only when HAL `+0x2e1 != 0` and
  image-valid `+0x237 == 1`. Arguments are HAL `+0x30c`, zero, and operation
  dword `+0x308`. The middle argument is unused by this owner.
- There is no sensor-health-history counterpart in
  `drivers/goodix53x5/device/{base,scan}.c`; image classification's broken-pixel
  masks are separate from this hardware health contract.

`GxFNHV_MilanOpen` (`0x18000450c:0x180004665..0x18000470b`) enables this
owner only for configuration byte `+0x424 == 1`. Enabled initialization sets
HAL `+0x2e4=200`, `+0x2e8=600`, `+0x2ea=300`, `+0x2e0=0`,
`+0x30d=1`, and `+0x30e=0`. Disabled initialization clears the three
threshold words and `+0x2e1`, sets `+0x30d=1` and `+0x30e=1`, and does not
write `+0x2e0`. Thus boot health initialization permits the first enrollment
sample independently of the boot count.

## Boot Producer And Pair Ownership

The optional startup producer is described in
[the all-base contract](usbinterface-FUN_180015c60.md#optional-sensor-check).
`FUN_180012d38` captures two TX-on/HV/no-finger images into global workspace
`0x1800615e8` and its `+30000` span, using modes 2/3 and DAC values HAL
`+0x2f6` / `uint16(+0x2f6 - +0x2fe)`. These are separate from retained base
images. Profile 9 adds no sleep in this helper. A first result of `-1` skips
the second capture. Workspace dword `+60000` becomes one after the first
non-`-1` capture and two after the second non-`-1` capture. This helper does
not reset that dword on entry and returns the final capture result.

After complete base admission, preparation result zero and HAL `+0x2e0 == 0`,
`FUN_180012c1c` computes a count through `FUN_180013578`, fills all thirty
history words at `[0x1800615d8]+8` with its low word, stores that word in
`0x1800615d0`, and calls record writer `FUN_180013130`. Its write result is
ignored. The base owner then sets `+0x2e0=1`. Boot measurement does not write
the two permission bytes or reject an otherwise admitted base.

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

## Publication Consumers

The up wrapper ignores a nonzero health-check return, signals HAL event
`+0x300`, and continues ordinary FDT-down rearming. A health failure is not
an up-handler failure.

`CaptureFramedone` (`0x18001fb40`) copies HAL `+0x30d` to image payload byte
`+2` when configuration byte `+0x424 == 1`; otherwise it writes one. See
[sample publication](usbinterface-FUN_18001fb40.md).
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
`drivers/goodix53x5/milan/runtime.c:goodix_milan_runtime_run`; its input
contains no hardware-health permission and its enrollment admission starts
with live preprocessing followed by quality/coverage checks.

The reported study byte has a different consumer boundary. Successful
one-byte deactivation I/O in engine `FUN_18001e230` replaces `+0x1b7` and
logs it. `EngineAdapterDeactivate` (`0x180023610`) then calls
`FUN_180031510` regardless of I/O result. That worker does not test `+0x1b7`
or pass it to its update wrappers: its arguments are the match byte,
template size, template destination, and action output. The health byte
therefore does not gate this deactivation study/storage path. See
[engine deactivation](FUN_180023610.md) and [update publication](FUN_180031510.md).
