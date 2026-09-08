# Profile 0 USB Contract

## Scope And Identity

This note owns the profile-0 USB initialization, calibration, and acquisition
contract in `usbinterface.dll` version 2.0.310.900, relative to profile 9.
Profile and algorithm sensor-type numbering are separate; see
[the mapping owner](CHIP-ID-PROFILE-MAPPING.md). Addresses use image base
`0x180000000`.

## Initialization And Geometry

`FUN_1800112ac` publishes the HAL context at `0x180061220`; its profile-0
branch calls `FUN_180001058` (`GxF_MilanOpen`). `FUN_1800162ac` publishes
`0x1800615f0` and calls `FUN_18000450c` (`GxFNHV_MilanOpen`) for profile 9.
Both copy the selected profile from input `+0x28` into HAL `+0x228`, publish
the HAL pointer at input `+0x60`, create eleven auto-reset events, and allocate
retained image, auxiliary, and 24-byte FDT buffers at HAL `+0x248`, `+0x258`,
and `+0x268`. Both reject null input, a null input `+0x68`, or an already
enabled family context.

The geometry table at `0x180035eb0 + profile*8` supplies columns, rows,
auxiliary columns, and auxiliary rows as its first four bytes:

| Profile | Main | Auxiliary | Main Bytes | Auxiliary Bytes |
| --- | --- | --- | --- | --- |
| 0 | 108 columns, 88 rows | 108 columns, 14 rows | 19008 | 3024 |
| 9 | 108 columns, 88 rows | 108 columns, 24 rows | 19008 | 5184 |

Thus both main planes contain 9504 samples; this geometry does not select the
USB callback contract. `GxF_MilanOpen` allocates two full-image work buffers
(`0x180060498`, `0x1800604a0`) and an auxiliary buffer (`0x1800604a8`).
`GxFNHV_MilanOpen` allocates one full-image work buffer (`0x180060708`) and
one auxiliary buffer (`0x180060710`). Both enable sensor checking only when
configuration byte `+0x424` equals one and use the common sensor-check
initializer `FUN_1800121a4` when HAL byte `+0x2e1` is nonzero.

### HAL Callback Owners

| Slot | Profile 0 | Profile 9 |
| --- | --- | --- |
| `+0x18`, OTP/check sensor | `FUN_180001470` | `FUN_180004a40` |
| `+0x40`, mode/config | `FUN_1800020f0` | `FUN_1800059c0` |
| `+0x68`, set FDT base | `FUN_180002090` | `FUN_180005950` |
| `+0x88`, transform FDT base | `FUN_180001370` | `FUN_1800048d0` |
| `+0x138`, read OTP | `thunk_FUN_18001b2e0` | same |
| `+0x180`, all-base acquisition | `FUN_180010ae0` | `FUN_180015c60` |
| `+0x188`, up | `FUN_180010920` | `FUN_180015aa0` |
| `+0x190`, down | `FUN_18000fdd0` | `FUN_180014e10` |
| `+0x198`, reverse | `FUN_1800108e0` | `FUN_180015a60` |
| `+0x1a0`, operation start | `FUN_1800105c0` | `FUN_180015710` |
| `+0x1a8` | `FUN_1800100d0` | `FUN_1800150e0` |
| `+0x1b0` | `FUN_180011100` | same |
| `+0x1b8` | `FUN_180011130` | same |
| `+0x1c0`, retry capture | `FUN_180010690` | `FUN_180015760` |
| `+0x1c8`, close | `FUN_180011160` | `FUN_1800160a0` |

Profile 0 installs main-image `FUN_180001d90` at `+0x58`, manual FDT
`FUN_180001be0` at `+0x78`, threshold read `FUN_180001d30` at `+0x80`,
and auxiliary-base capture `FUN_180001ed0` at `+0x90`. Profile 9 instead
installs parameterized image capture `FUN_1800055d0` at `+0x158` and
TX-selectable FDT `FUN_180005420` at `+0x160`.

## OTP And Configuration

Both check-sensor callbacks read 32 OTP bytes with the shared
`thunk_FUN_18001b2e0(..., 0x20, 200)`. Both optionally replace the chip OTP
with the 32-byte result of `FUN_18001799c` only when that call succeeds and
its first sixteen bytes match the chip. Both pass the selected profile to
`FUN_180017a84`, retain successful OTP at HAL `+0x205`, and publish check
status at `+0x1e8`.

Profile 0 (`FUN_180001470`) derives calibration from OTP bytes 22 and 23.
When byte 22 is zero or their unsigned sum is not `0xff`, both TCODE at
`0x18006048a` and difference at `0x18006048c` are zero. Otherwise:

```text
tcode = ((otp[22] >> 4) + 1) * 16
diff = ((((otp[22] & 15) + 2) * 25600 / tcode) / 3) >> 4
```

Integer divisions truncate. The values pass to `FUN_1800095c0`. After the
OTP-validation branch, the owner calls `FUN_180001a30(2)` and copies HAL
`+0x22c/+0x22e` into sensor-check DAC fields `+0x2f6/+0x2fe`.
`FUN_180001a30` issues category 7 command 0 with timeout 500, reads register
`0x220` into `+0x22c` and register `0x5c` into a local word, and calls
`FUN_180009b48(2, ...)` after each read. For argument 2 and nonzero read
TCODE it writes `+0x22e = 0xa00 / tcode`; zero TCODE leaves that field
unchanged. These DAC fields come from register reads, not profile 9's packed
OTP DAC formula.

Profile 9 (`FUN_180004a40`) instead derives TCODE from OTP byte 23 (zero
stays zero; otherwise byte plus one), difference from `(otp[17] >> 1) & 31`,
and packed DACs from bytes 17, 22, and 31. Its exact thresholds and fallback
values are owned by [the profile-9 calibration note](functions/usbinterface-FUN_180004a40.md).

Mode 4 uses the common `FUN_1800180f8` to copy 256 bytes from
`0x18005f3a0 + profile*0x100`, selecting `0x18005f3a0` for profile 0 and
`0x18005fca0` for profile 9. The common checksum is:

```text
LE16(config + 0xfe) = uint16(-(0xa5a5 + sum(LE16(config + 2*i), i=0..126)))
```

Profile-0 mode owner `FUN_1800020f0` conditionally calls `FUN_180018420`
with nonzero TCODE and `FUN_180018264` with `(diff << 8) | 0x80` when
difference is nonzero. It downloads with `thunk_FUN_18001aed8`, length
`0x100`, timeout 2000. Profile-9 `FUN_180005094` additionally patches TCODE
through `FUN_180018320` and `FUN_1800185f4`, and conditionally patches DACs
through `FUN_18001819c` and `FUN_180018528`; its download timeout is 500.
See [the configuration owner](functions/usbinterface-FUN_180005094.md).

The compiled profile-0 configuration starts with byte `0x00`, versus
profile 9's `0x40`. Its section base/length pairs are `11/54`, `65/24`,
`89/24`, `ad/1c`, `c9/1c`, `e5/04`, `e9/04`, and `ed/13` (hex).
The profile-0 TCODE helper patches section 4 tag `0x005c`, whose value is
at offset `0xcf` (compiled `0x0080`); the difference helper patches section
2 tag `0x0082`, value offset `0xab` (compiled `0x1580`). The final checksum
is recomputed after copying and after each invoked patch.

## Profile-0 Base Acquisition

`FUN_180010ae0` (`Milan_update_allbase`) allocates auxiliary and full-image
temporaries and first calls `FUN_18000d24c` to load persisted bases. That
loader uses HAL `+0x231` for persisted-base presence and, on an admitted
record with matching first sixteen OTP bytes, restores FDT, auxiliary, and
image data and invokes `+0x68` with the retained FDT base.

The acquisition sequence is:

1. Mode 4 through `+0x40`.
2. First 12-word FDT sample through `+0x78`.
3. Auxiliary-base acquisition through `+0x90`.
4. Second 12-word FDT sample through `+0x78`.
5. Read threshold through `+0x80`, then shift that word right by eight.
6. Reject when any `abs(first[i] - second[i]) > threshold`; equality passes.
7. Main-image acquisition through `+0x58` and third FDT sample through `+0x78`.
8. Apply the same predicate to second versus third FDT samples.
9. Transform the third sample through `+0x88`, retain its 24 bytes at
   `+0x268`, and publish it through `+0x68`.

The FDT comparisons use full unsigned 16-bit samples, not profile 9's
per-operand right shift. Validation rejection loops back to mode 4 when
`+0x231 != 1`; when `+0x231 == 1`, it exits instead. There is no numeric
retry cap in this owner. A callback return of `-1` exits the sequence.

Profile-0 base transform `FUN_180001370` writes
`uint16((sample | 1) << 7)` for each of twelve words. Profile 9 uses a
different transform; see [its owner](functions/usbinterface-FUN_1800048d0.md).
The threshold callback `FUN_180001d30` issues category 7 command 0 with
timeout 200, then calls `FUN_1800180b0(0x82, output)` and
`FUN_180009b48(2, output)`.

After FDT admission, retained image/auxiliary publication is handled by
`FUN_180009904` and `FUN_1800099b4`, which copy the profile-0 global work
buffers `0x180060498` and `0x1800604a8`, respectively, not the local
temporaries. When a persisted base exists, `FUN_1800095e8` and
`FUN_1800096d4` first assess whether each retained plane needs replacement.
Replacing an existing image sets bytes `+0x236/+0x237` to one. The existing
base route computes `+0x232` through `FUN_1800095c8`.

Profile-9 acquisition instead uses TX-on FDT, TX-on/HV image, TX-off FDT,
TX-off/HV image, and final TX-on FDT, including a separate image-pair
admission predicate. It retains the first TX-on image only after complete
admission and has no profile-0-style validation retry loop. See
[the acquisition owner](functions/usbinterface-FUN_180015c60.md).

## Profile-0 Image Command

Both main-image `FUN_180001d90` and auxiliary-base `FUN_180001ed0` call
`FUN_180001e50`. It zeroes a 64-byte local buffer, sets its first word to
one, and invokes `FUN_180017ec0(2, 0, 0, buffer, 500)`. It writes HAL
`+0x200 = 4` and returns `-1` if the dispatcher returns false. The wrappers
then retrieve main/auxiliary data through `FUN_180007058`/`FUN_1800070b8`.
Unlike profile-9 `+0x158`, this callback ABI has no TX/HV/DAC parameters.

Manual FDT `FUN_180001be0` builds mode byte `0x0d` followed by twelve words
`(retained_word & 0xff80) | 0x80`, dispatches category 3 command 3 with
timeout 500, and copies the twelve response words from `0x1800604bc` on
success. Failure returns `-1` without publishing a response vector.

The shared `FUN_180017ec0` wrapper calls `FUN_180019ec8` (`ChangeMode`).
Its category-2 branch sends exactly four payload bytes through
`FUN_180018dd8`, with ACK timeout 500, caller-supplied response timeout,
and response-event selector 8. Profile 0 therefore sends `01 00 00 00`.
Profile-9 `FUN_1800074bc` sends byte zero `0x01` (TX on) or `0x81` (TX
off), OR `0x40` for a finger image; byte one is configured HV when enabled
or `0x10` when disabled; the last two bytes are LE16 DAC. It explicitly
rejects raw-data status `0x180060cc0 == -1` as well as command failure.
The profile-0 image wrapper has no corresponding raw-status-global test.

`FUN_1800070b8` derives the profile-0 auxiliary plane from the decoded
main-image global. It starts at byte offset `0xa20`, copies 108 words per
row, and advances the source by `0x360` bytes for fourteen rows. For the
108-column main plane this selects zero-based rows `12 + 4*r`, `r=0..13`.
The output rows are contiguous. The auxiliary-base callback acquires its own
main frame before this extraction; the later main-image base acquisition is
a separate command.

## Shared Decode Boundary

Profile-0 raw callback `FUN_180001300` invokes `FUN_1800071d8`; profile 9
uses `FUN_180007968`. Both allocate two temporary buffers of incoming
length plus `0x400`, invoke the same reply processor `FUN_180024940` with
context `0x180060b90`, and call `FUN_180009b88` with mode zero and HAL
byte `+0x1ec` before decoding. Both pass the post-processing buffer and its
published byte count minus four to the same raw12 converter `FUN_180009a64`.
Their destinations are the respective profile globals `0x180060498` and
`0x180060708`. Successful conversion is followed by
`FUN_18000dd84` with event value 8. Earlier reply-processing or CRC failure
does not decode or publish that event in either owner.

See [the decoder owner](functions/usbinterface-FUN_180009a64.md) for the
six-byte/four-word packing formula. This shared call boundary does not make
the calibration or reference-admission contracts interchangeable.

## Capture Decisions And Rearming

Both operation-start callbacks (`FUN_1800105c0`, `FUN_180015710`) request
mode 4, ignore that callback's return, then return the result of `+0xb0(1)`.
The corresponding rearm owners (`FUN_180002230`, `FUN_180005a60`) send
category 3 command 1 for argument one (down) or command 2 for argument zero
(up), with timeout 500 and their family-specific base store. When the first
dispatcher result satisfies `(result & 3) == 3`, they reload mode 4 and
repeat the arm command. They publish HAL `+0x1fc = 0xf0/0xf1` and return
zero rather than propagating the arm result.

Profile-0 retry `FUN_180010690` returns no image immediately when HAL
`+0x200 == 10`. Otherwise, after timer cleanup and the `+0x235 == 1`
early-return gate, it captures through `+0x58` before calling
`FUN_1800095e8` with the retained base at `+0x248`. That helper calls
`FUN_180006640` with the retained base and newly decoded global image.
Result one publishes output byte `+4 = 1`, copies the image to output `+5`,
and rearms up. Results two or three clear the output flag and rearm down;
result zero clears the flag and may invoke `FUN_18000f894` after timer
cleanup. The profile-0 ordinary image owner `FUN_1800100d0` uses the same
classification helper and invokes completed-frame callback `+0x240` only
for result one, passing the retained reference and captured image.

Profile-9 retry `FUN_180015760` instead first acquires TX-on and TX-off FDT
samples and compares them with `FUN_180014c98`. Only a failed equality-band
comparison (some `abs((on[i] >> 1) - (off[i] >> 1)) > HAL_word_31c`)
enters image acquisition through `+0x158`, with TX and HV enabled and DAC
pointer `+0x312`. A passing FDT comparison reports no image and rearms
down. The profile-0 image-first classification is not used on this route.

## Image And Auxiliary Classifier

`FUN_1800095e8` selects mode zero in classifier context `0x180060cd0` and
calls `FUN_180006640(reference, current)` through its context-first ABI.
`FUN_1800096d4` calls `FUN_180007d88`, which selects mode one, temporarily
sets the context vote-count byte `+8` to zero, calls the same classifier,
and restores the vote byte. Mode one remains selected until another owner
changes it. Both wrappers compare retained planes against their respective
profile-0 current global buffers.

`FUN_180006124`, reached from OTP calibration through `FUN_1800095c0`,
replaces an all-zero `(diff,tcode)` pair with `(21,128)`. Profile zero calls
`FUN_1800061c8(7,256,uint8(diff),tcode,88,108,128,12,4,14)`.
The profile-0 branch initializes a 3-by-4 window grid. Main-image row origins
are `[8,40,72]`, column origins `[8,36,64,92]`; windows are 8-by-8.
Auxiliary row origins are `[0,7,12]`, the same column origins, and windows
are 2-by-8. Main thresholds at context `+0x54/+0x58` both equal
`floor(tcode * uint8(diff) / 16)`; auxiliary thresholds `+0x64/+0x68`
both equal `8 * uint8(diff)`. Thus all four default thresholds are 168.

For every window, `FUN_180006640` computes reference mean, current mean,
and mean absolute pixel difference with unsigned integer sums and truncated
division by 64 (main) or 16 (auxiliary). It then sums squared deviations of
absolute differences from that truncated difference mean and divides by 63
or 15. The four scratch arrays at context `+0x30/+0x38/+0x40/+0x48` are
zeroed at entry and overwritten. Neither input image is modified.

Let `V` be the selected variance threshold, `A` the selected difference
threshold, and `K = max(1, context_byte_8)`. Main uses `K=7`; the auxiliary
wrapper forces `K=1`. Multipliers below use double arithmetic followed by
integer truncation, matching the native conversions:

- `N`: number of windows with variance strictly greater than `trunc(1.4*V)`.
- `M`: number with `trunc(0.6*A) < mean_abs_diff < trunc(1.4*A)`.
- `H`: any window with `mean_abs_diff > trunc(1.4*A)`.
- `B`: any window with `current_mean > reference_mean + trunc(1.4*A)`.
- `P+`: count of pixels with `current > reference + 32`.
- `P-`: count with `reference > current + 32`.

Pixel counts cover main rows `1..86`, columns `1..106`, or all fourteen
auxiliary rows and columns `1..106`. Their ten-percent cutoffs are 911 and
148 respectively, computed by truncation. The return decision is exactly:

```text
if N < K:
    if not H: return 2 if M < K else 0
    if B: return 3
else:
    if P+ >= cutoff and P- < cutoff: return 3
return 1
```

Result one is the image-present/retain-reference route in callers; zero
selects full refresh in live capture; two and three select their respective
rearm or reference-update branches. These numeric results are not Boolean
admission flags.

### Stateful Validity Reduction

`FUN_1800095c8` forwards image and auxiliary result bytes to
`FUN_18000658c`, which owns prior combined result `0x18005e1ec`.
For valid result values 0..3, the next combined result is:

| Image Result | Auxiliary 0 | Auxiliary 1 | Auxiliary 2 | Auxiliary 3 |
| --- | --- | --- | --- | --- |
| 0 | 0 | 1 | 0 | 3 |
| 1 | 1 | 1 | 1 | 1 |
| 2 | 0 | 1 | history branch | 3 |
| 3 | 3 | 1 | 3 | 3 |

Every ordinary table entry stores the combined result and writes validity
zero. For `(2,2)`, if the previous combined result is one of `0,2,3`, it
leaves that history unchanged and writes validity one. Otherwise it stores
two and writes validity zero. Thus validity is stateful; two individually
no-change classifications do not reduce to a context-free Boolean rule.

The compiled prior result is one. Neither profile-0 classifier initialization
nor HAL setup resets it; `FUN_18000658c` owns its direct writes. From that
fresh prior state the first `(2,2)` stores two with validity zero, and a
subsequent `(2,2)` publishes validity one. Module lifetime and prior calls
therefore matter independently of the two current image planes.

## Cold Start And Reference Recovery

With no persisted base, `FUN_180010ae0` copies admitted current image and
auxiliary planes into retained storage but does not set `+0x232` or
`+0x237` on that branch. The later profile-0 up procedure
`FUN_180010920` calls `FUN_18000f60c` (`Milan_checkbase_isok`) unless
refresh-in-progress byte `+0x235 == 1`.

When `+0x232 == 0`, `FUN_18000f60c` captures and classifies a new main
image, replaces the retained image unless the result is one, and sets both
`+0x236/+0x237` to one only for image result zero or two. It then captures
and classifies a separate auxiliary frame and replaces its retained plane
unless the auxiliary result is one. If either result differs from one it
calls persistence owner `FUN_18000da18`, then invokes the stateful validity
reducer above to write `+0x232`. Callback `-1` aborts the remaining sequence.
An already-valid base skips acquisition. This is a later validity producer,
not an implicit success write in initial all-base acquisition.

`FUN_18000f894`, called for classifier result zero by live capture/retry
and the classification timer, sets `+0x235=1`, clears both `+0x232` and
`+0x237`, calls `FUN_180010ae0`, clears `+0x235`, and rearms down through
`+0xb0(1)` when installed. Its rearm return replaces the acquisition return.
Unlike profile-9 temperature refresh, it explicitly invalidates the image
before reacquisition.

## FDT Events And Refresh Ownership

Profile-0 parser `FUN_180002430`, installed at `+0x148`, handles selector
3 first: copies payload bytes `4..27` to manual FDT store `0x1800604bc`,
retains the touch word from payload `+2`, and signals HAL `+0x2d8` without
publishing an asynchronous worker event. Otherwise:

| IRQ | Selector | State And Event |
| --- | --- | --- |
| 2 | 1 | Save down FDT at `0x180060504`, retain touch word, set `+0x200=9`, event 15 |
| 2 | 2 | Save up FDT at `0x18006051c`, set `+0x200=10`, event 16 |
| `0x80` or `0x82` | nonmanual | Save up/reverse FDT at `0x18006051c`, set `+0x200=15`, event 17 |

IRQ 2 with another nonmanual selector still publishes event 16 but does not
perform either selector-specific data/state write. IRQ values 0, 1, 4, 8,
`0x10`, `0x20`, and `0x40` return without waking the worker. Remaining
values publish state `0x30` and event 20. In particular `0x200` is not the
profile-0 up IRQ contract. The parser signals HAL `+0x10` after publishing
the event and does not test arm state `+0x1fc`.

Both profiles use worker `FUN_18000df20`, created by `FUN_18000e138`, with
one event-type slot and a manual-reset event, not a packet queue. It waits
indefinitely, resets the event, and synchronously dispatches events 15/16/17
as actions 0/1/4 through `FUN_18000e1f0`. That common dispatcher holds
critical section `0x18005e7c8` while invoking family callbacks.

Profile-0 reverse `FUN_1800108e0` only calls `+0x110(0)` to update the down
base, then `+0xb0(1)` to rearm down; it does not classify an image or refresh
all bases. Profile-0 up `FUN_180010920` updates that down base and invokes
`FUN_18000f60c` unless `+0x235==1`, performs optional sensor checking,
stops/clears timer `+0x238`, and rearms down unless refresh is in progress.
Profile-9 reverse/up instead own software-anchor drift decisions and may
call full-base acquisition; see the existing
[event-loop owner](functions/usbinterface-profile9-fdt-event-loop.md).

The profile-0 `+0x110` owner `FUN_180002320` performs these base updates
in worker context, rather than in the packet parser. Argument zero transforms
the up/reverse sample in place with `uint16((sample | 1) << 7)` and copies
it to down-arm store `0x1800604d4`. Argument one calls `FUN_18000139c`
on down samples and copies the result to up-arm store `0x1800604ec`.
For touch-mask bit `i` set, `FUN_18000139c` writes
`uint16((((sample[i] >> 1) + d) << 8) | 0x80)`; for a clear bit it writes
`uint16(((d-2) << 8) | 0x80)`. Here `d` is calibrated difference or 21
when that difference is zero. The touch mask is retained from the down
packet before worker dispatch.

### Down And Timer Paths

`FUN_18000fdd0` clears study-status byte `+0x30e` and resets sensor-check
event `+0x300` when checking is enabled. With screen-state global
`0x18005f398==0` and device wake-on-finger byte `+0x151==1`, it stops
timer `+0x238`, skips capture if `+0x235==1`, otherwise calls cached-frame
owner `FUN_1800102f8`. The ordinary screen-on image path requires HAL
`+0x1e0==0`, screen-state nonzero, `+0x235!=1`, and image-valid
`+0x237==1`, then calls `FUN_1800100d0`. Neither route checks FDT-base
validity `+0x232` before live capture. After capture, classification result
one at `+0x284` calls `+0x118`, then rearms up; other results return the
capture owner's result. A no-capture path creates timer `+0x238` if absent,
updates the up-arm base with `+0x110(1)`, and rearms up.

The successful-image `+0x118` callback is `FUN_1800018d0`: it snapshots
the down packet and touch mask, acquires another manual FDT sample, chooses
the minimum of down/manual for each of twelve areas, ORs their touch masks,
then calls `FUN_18000139c` and publishes the result to the up-arm base.
The manual read's return is returned but does not gate that transformation;
its destination starts zeroed. The down owner ignores this callback return
and proceeds to up rearming.

`FUN_18000f8ec` creates a nonperiodic framework timer with callback
`FUN_18000f9e0` and relative due time `-50000000` in 100-ns units (5 s).
That timer captures through `+0x58` and classifies against the retained image:
result zero invokes `FUN_18000f894`, one rearms up, and two/three rearm
down. It does not itself install a completed-frame callback. This is a
profile-0 image/classification timer, distinct from the profile-9 cached-frame
expiry timer described in the event-loop note.

## Sensor-Check Acquisition

`FUN_180012d38` has explicit profile-0 and profile-9 acquisition branches
against the common sensor-check context. Profile 0 programs the default DAC
word `HAL+0x2f6` through `+0xf8`, captures through `+0x58` into the first
workspace plane, programs `uint16(default + 16*HAL_word_2fe)`, then captures
the second plane at workspace `+30000`. Successful completion restores the
default DAC and sleeps 10 ms. A failed intermediate DAC write or image call
does not run an unconditional restore postlude. Workspace dword `+60000`
records one and then two acquired planes. `+0xf8 = FUN_180002050` issues
category 7 mode and writes register `0x220` through `FUN_1800180d4`.

Profile 9 instead passes default and `default-delta` DAC words directly to
`+0x158` with TX/HV enabled, capture modes 2/3, and no fixed sleep in this
producer. No profile-0 register-DAC mutation is required on that branch.

Profile-0 boot checking occurs in `FUN_180010920` after image validity:
when `+0x2e1!=0`, `+0x237==1`, and boot-check byte `+0x2e0==0`, it calls
preparation and, only on zero return, `FUN_180012c1c` and sets `+0x2e0=1`.
Later up events call `FUN_180011b9c` instead. This differs from profile 9,
whose all-base owner can initialize sensor-check history during initial
reference acquisition. The common boot helper calls `FUN_180013578`, fills
thirty history words with the low sixteen bits of its count, updates the
retained count, and invokes `FUN_180013130`. Up signals event `+0x300`
after its checking branch and continues normal rearming even on check error.

The common count owner `FUN_180013578` has no profile branch. Dimensions
come from HAL during `FUN_1800121a4`; both profiles therefore scan the same
88-by-108 main geometry. Border exclusion word `0x18005e800` is compiled
as one. For decoded raw12 input, it averages absolute pair differences
strictly between 800 and 1200 (`801..1199`). If there are none, it reports
every interior pixel as broken. Otherwise it counts differences strictly
outside the inclusive interval `[mean-HAL_word_2e4, mean+HAL_word_2e4]`.
The default half-width is 200 for both profiles.

After family-specific preparation, common `FUN_180011b9c` calls that count
owner and history filter `FUN_1800138bc` without another profile dispatch.
For nonzero operation, study byte `+0x30e` becomes one only when both raw
and filtered counts are below HAL word `+0x2ea` and their absolute difference
is below 20; next-enrollment byte `+0x30d` follows raw count `< +0x2e8`.
For operation zero, next-enrollment requires filtered count `< +0x2e8`
and either raw count below that threshold or the history-spread result of
`FUN_1800137cc` greater than 10. Successful preparation returns zero after
these status decisions even when a status byte is disabled. Default
enrollment/study count thresholds are 600/300 for both profiles.

## Shared Session Dispatch

Full initialization `FUN_180020970` calls `FUN_18000e9b0` before action 9
(family check-sensor). `device_enable` sends the common sensor reset through
`thunk_FUN_18001b6c8(0,NULL)`, sleeps 10 ms, then discovers the profile with
`FUN_180017ef8`. Only then does it select `FUN_1800112ac` for profile zero
or `FUN_1800162ac` for profile nine. Successful setup starts the same
controller worker. Reset therefore precedes family selection.

After successful family sensor check, both enter `FUN_180007ee0` with the
device ring pointer `+0x190` and handshake-complete dword `+0x198`. That
owner has no profile predicate: under its common critical section it clears
completion/ring indices, calls `FUN_180008398` to initialize the client and
`FUN_18000823c` to handshake. The former calls the common credential provider
and `FUN_180025930` with the same send/receive callbacks
`FUN_18001c2f0/FUN_18001c5a0`; the latter calls `FUN_180025230` on the
same session context. Credential contents are not a profile-dispatch input
at this boundary.

The wrapper makes at most three initialization attempts. The negative
in-progress status `-0x400401` permits up to five handshake calls per
attempt with 5-ms sleeps between continuing calls; unsuccessful attempts
sleep 10 ms before the next attempt. `deviceInit` retries the whole wrapper
once on any first nonzero return, additionally clearing protocol caches via
`FUN_18001b94c` only when the absolute error is `0x700003`.

After session success, both dispatch persisted-base action 10 and all-base
action 12, query firmware version, then request mode 2. The full-init tail
does not use the action-12 return as an image-validity gate. Family-specific
behavior resides in those dispatched callbacks, not a second session protocol.
The initialized-device resume branch likewise uses the common handshake
wrapper when system-power state `+0x168>=2`, without reset, family selection,
OTP, or base acquisition. See
[the initialization owner](functions/usbinterface-FUN_180020970.md).

The approved profile-0 image command remains the four-byte payload
`01 00 00 00` established above; its first word being one does not reduce
the shared command producer's transmitted length to two bytes.

The session receive callback `FUN_18001c2f0` waits up to 2000 ms on device
event `+0x1a0`, then reads the ring at `+0x190`. Send callback
`FUN_18001c5a0` sends category 13 command 1 with ACK timeout 500, no data
response wait, and a subsequent 2-ms sleep. Neither callback branches on
profile. These are shared call-path contracts, not a claim that factory
credential provisioning is identical on different machines.

## Request And Shutdown Ownership

Common request handler `FUN_180021978` owns pending request `device+0xf8`,
registers cancellation `FUN_180020ef0`, waits up to 3000 ms for session
completion, and calls `FUN_18000ebcc` with completion owner
`FUN_18001fb40`. `FUN_18000ebcc` selects the existing up/down arm from
screen/wake state, cancellation-restart byte `device+0x154`, and HAL
`+0x1fc`, then publishes the non-null callback at HAL `+0x240`. Both
families use this owner. Its ordinary request path does not invoke the
family operation-start callback `+0x1a0`; that callback is separately reached
through action 3. A subsequent request after cancellation clears
`device+0x154`, stops timer `HAL+0x238`, and rearms down on its screen-on
restart branch.

Profile-0 normal capture `FUN_1800100d0` allocates `rows*columns*4` bytes,
although its `+0x58` image callback writes only `rows*columns*2` bytes.
On classifier result one it calls `+0x240(device, retained_reference,
capture_buffer, rows*columns*4, HAL_byte_236)`, then clears the one-shot
marker and callback and frees the temporary. The fifth argument is written
explicitly at `0x18001024b`; it is not absent despite the decompiler's
four-argument rendering. Callback size is explicitly four bytes per pixel
at `0x18001012c` and `0x18001023e`. The second half is not initialized by
this acquisition owner. The shared completion owner copies the supplied
capture length but only `rows*columns*2` bytes from the retained reference.
The native callback-size contract must not be inferred from the raw12 plane
length alone.

Cached wake-on-finger capture `FUN_1800102f8` uses the same four-byte-per-
pixel allocation size and, for classifier result one, retains a copy at
HAL `+0x340`, with byte size at `+0x34c`, under critical section `+0x368`.
`FUN_18000f444` installs expiry callback `FUN_18000fc10` on timer `+0x360`:
timeout is configuration byte `+0x441` seconds, or three seconds when zero.
Expiry frees/clears `+0x340` under that critical section without acquiring
an image or rearming FDT. Common action 21 can instead consume the cached
frame, call `+0x240` with the retained reference and one-shot marker, then
free/clear the cached image and callback. Profile-0 up does not perform
profile 9's power-button-conditioned cached-frame deletion.

Cancellation `FUN_180020ef0` takes the common construction/request locks,
sets device stop bytes `+0x154/+0x152`, completes with `0xc0000120`, and
clears pending request `+0xf8`. It does not clear HAL `+0x240`, invalidate
bases, stop the HAL worker, or destroy the session. `FUN_18001fb40` stages
the frame in common storage but publishes to a client only with both a
pending request and output pointer. `FUN_18001ff08` unmarks cancellation
and completes/clears the pending request unless unmark returns
`0xc0000120`, leaving that completion to the cancellation owner. Request
reset `FUN_18002296c` uses this completion helper and returns an eight-byte
success response, without a hardware reset or family teardown.

There is no cancellation-byte check inside `FUN_180010ae0`'s reference
validation retry loop. Request cancellation therefore does not itself
interrupt a running profile-0 all-base attempt. The common worker executes
that callback synchronously while holding the action critical section.

Hardware release `FUN_180023c40` first marks the device stopping and waits
for initialization, then calls common `FUN_18000e8cc`. That disable owner
calls `FUN_18000e184` before the family close callback: it sets HAL event
type 22, clears worker-run byte `0x180061218`, signals HAL `+0x10`, waits
up to 20000 ms for the worker, and closes its handle. The wait result is not
used to block subsequent close. Thus a bounded shutdown wait is separate
from cancellation of an in-progress acquisition callback.

Profile-0 close `FUN_180011160` clears enabled byte `+0x204`, stops timer
`+0x238` if present, frees the chip work buffers through `FUN_180001000`,
destroys optional sensor-check state, closes the worker event and eleven
completion events, stops/clears cached-frame timer `+0x360`, deletes its
critical section, and frees/clears the pointer at `+0x350`. That pointer is
distinct from the cached-image owner `+0x340`. It contains no
free of retained image/auxiliary/FDT buffers `+0x248/+0x258/+0x268`.
Profile-9 close `FUN_1800160a0` explicitly frees and clears all three
retained buffers after its corresponding cleanup. This is an ownership
difference at the close callback boundary, not interchangeable cleanup.
After family teardown, hardware release destroys the common protocol and
session ring/events through `thunk_FUN_18001b40c` and `FUN_1800208c4`.

## Engine Capture-Payload Consumer

The consumer in this section is `GoodixEngineAdapter.dll` 2.0.310.900;
all other unqualified addresses in this note belong to `usbinterface.dll`.
Engine `FUN_18001f610` accepts the complete sample. Let `F` be its image
record (`sample + LE32(sample+0x0c)`) and `P=F+0x38` its image payload.
At `0x18001f886..0x18001f8fc`, assembly computes:

```text
bytes_per_pixel = ceil(F[0x22] / 8)
L = LE16(F+0x2c) * LE16(F+0x2e) * bytes_per_pixel
```

The supplied header has bit depth 16, so both profiles use `L=19008`.
The decompiler's Boolean rendering of the bit-depth expression omits its
quotient term; the assembly includes both integer division by eight and the
nonzero-remainder increment.

The engine allocates a `2*L` live-image temporary and an `L` reference
temporary, then makes three distinct copies:

| Engine Call | Source | Destination | Bytes |
| --- | --- | --- | --- |
| `0x18001fc13` | `P+3` | live temporary | `L` |
| `0x18001fc58` | `P+3+L` | live temporary `+L` | `L` |
| `0x18001fc86` | `P+0xebf0` | reference temporary | `L` |

It also copies the entire input sample into retained engine-context `+0x38`
and records the complete sample length at `+0x40`. Thus the second live
span is read and marshalled; it is not eliminated by the accept-sample ABI.

The algorithm-input boundary is narrower. Engine `FUN_180031d00` receives
the reference and length `L` and, on successful initialization, stores `L`
at context `+0x80`. `FUN_180032040` forwards the live temporary pointer
with that stored length to `FUN_18002c810`. The latter allocates/copies only
that length, requires `L < 0x75f9` and equality to the selected geometry times
two, and gives
the resulting one-plane descriptor to `preprocessor`. It never advances
the input pointer to the second span. Both profiles use this same wrapper
boundary; no profile-zero branch forwards the second span as another image.

Consequently the profile-0 four-bytes-per-pixel callback length changes
sample contents and accept-sample copying, but does not supply a second image
to ordinary live preprocessing. Only the first initialized `L` bytes reach
that algorithm-input descriptor. The uninitialized second half supplied by
the profile-0 capture owner must not be treated as meaningful second-image
data or as deterministic sample-envelope bytes.

The USB completion callback's fifth argument is not padding. USB
`FUN_18001fb40` stores it at payload byte `P[0]`. Engine
`FUN_18001f610:0x18001fb5d..0x18001fb6f` loads that byte and retains it at
context dword `+0x98`; `0x18001fec0..0x18001fed0` calls setup when
initialized byte `+0x7c` is zero or the marker is exactly one. Setup uses the
separate reference plane, not the surplus live span. The branch has no
profile-zero/profile-nine distinction. Other nonzero marker values do not
force reinitialization of an already initialized engine.

## Sensor-Info Identity Producer

USB `FUN_1800222a0` (`OnGetSensorInfo`) produces the 48-byte sensor-info
response consumed by the adapter's storage identity owner. It waits up to
40000 ms for device initialization, then copies these device-context fields:

The wait result is not checked before those copies. A returned response is
therefore not itself proof that initialization completed successfully.

| Response Offset | Size | Source |
| --- | --- | --- |
| `+0` | 1 | length `0x30` |
| `+1` | 1 | one iff at least one of response identity bytes `+2..+17` is nonzero |
| `+2` | 32 | device `+0x36..+0x55`, retained OTP |
| `+0x24` | 4 | device dword `+0x58` |
| `+0x28` | 4 | device dword `+0x5c`, selected profile |
| `+0x2c/+0x2d` | 1 each | device geometry bytes `+0x60/+0x61` |

The identity consumed by the adapter is exactly response bytes `+2..+17`,
the first sixteen OTP bytes. `FUN_180020970` supplies device
`+0x36..+0x55` by copying HAL `+0x205..+0x224` after successful check-sensor.
The two check-sensor owners `FUN_180001470` and `FUN_180004a40` retain
the selected 32-byte OTP at that HAL offset after validation. Their optional
file replacement requires the first sixteen file/chip OTP bytes to match,
so it does not change the identity.

OTP validation selects `FUN_180017bbc` for profile zero and
`FUN_180017918` for profile nine through `FUN_180017a84`. These are
different OTP-integrity predicates, not additions of a profile or chip ID to
the first sixteen bytes. Their successful normalization writes affect OTP
bytes 26..28, outside the identity. Neither the response producer nor this
identity provenance chain hashes, appends, or compares the selected profile
or detected chip family inside the sixteen-byte identity. The profile is
exported separately at response `+0x28`.

Accordingly the sixteen-byte equality contract documented in
[the matcher/storage owner](PROFILE0-MATCH-TEMPLATE-CONTRACT.md#adapter-storage-identity)
is sensor-identity equality, not proof of algorithm-subtype equality. These
native producers establish no invariant that equal first-sixteen OTP bytes
imply equal selected profile. The sensor-info nonzero byte likewise proves
only that the identity is not all zero, not its subtype or uniqueness.

## Sensor-Check History

`FUN_1800137cc` and `FUN_1800138bc` contain no subtype/profile dispatch.
Their inputs are unsigned 16-bit count history, not images or profile fields.
`FUN_1800137cc` computes a nearest-integer mean (`sum/n + 0.5` followed
by truncation), writes that word, computes mean squared deviation from the
rounded mean, and returns the rounded square root. Its actual callers use
positive small counts: eight in the filter and up to four in enrollment
history assessment. It does not implement a separate profile-zero statistic.

`FUN_1800138bc(new_count, history[30], prior_filtered)` selects its output
from the pre-insertion history in this order:

1. If the first four words are equal, use that value.
2. Otherwise, when the signed 16-bit eight-entry spread is below five and
   is not `-1`, use the eight-entry rounded mean.
3. Otherwise count, for every history value, all thirty neighbors whose
   absolute difference is below five. Select the first maximum-count center
   (ties retain the earlier index), and compute its neighborhood's truncated
   mean. Use that candidate only when the neighborhood has at least ten
   entries and `prior_filtered >= candidate + 600`; otherwise retain prior.

After selection it calls `FUN_180013b1c` to insert the new count, writes the
selected result through `prior_filtered`, and returns its low sixteen bits.
The new count is not included in the current filtering decision.

The insertion helper depends on image dimensions and border exclusion, not
subtype: with the shared 88-by-108 geometry and one-pixel border its ceiling
is `trunc(9116*0.9)=8204`. A new count greater than 8204 is replaced with
`601 + random_u16 % 7604`; otherwise it is retained. The helper shifts the
old first 29 words right and inserts the value at index zero. Both profiles
therefore use the same history logic and geometry-dependent ceiling; their
different sensor-check acquisition and timing produce the input counts.
