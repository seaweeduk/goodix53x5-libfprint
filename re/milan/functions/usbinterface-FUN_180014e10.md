# usbinterface.dll FUN_180014e10

## Identity

- Address: `0x180014e10`
- Logged name: `MilanHV_Down_procedure`
- Role: profile-9/type-12 FDT-down handler that distinguishes drift/noise from
  a real touch before live-image capture.

## Profile-9 Dispatch

`FUN_1800162ac` installs this function in the profile-9 callback table at
`0x18001637d`. The lack of a normal direct caller is therefore expected; the
hardware event path invokes it through that table.

The complete scheduler is documented in
`usbinterface-profile9-fdt-event-loop.md`. In summary, the MCU packet parser sets
worker event `0x0f` and wakes a controller thread that was blocked indefinitely
on an event. The thread dispatches this function synchronously; it is not called
by a periodic temperature task.

## Down-Event Decision

Before choosing either image route, enabled health checking (`+0x2e1 != 0`)
clears study-status byte `+0x30e` and resets health event `+0x300` at
`0x180014ed9..0x180014ef0`. This has no capture-request or mode predicate.
The up wrapper later performs the optional health pair and signals the event
when health checking is enabled and image-valid is one. Deactivation's health
wait/output consumer is documented in `usbinterface-FUN_180011b9c.md`.

The first capture-route selector tests screen-active global `0x18005f398 == 0`
and device-context byte `+0x151 == 1`. That branch calls
`MilanHV_ReadImg_ForWOF` (`0x18001545c`), sets the screen-active global to one,
and dispatches action `0x15` for any eligible retained-image delivery. It
bypasses the ordinary manual-FDT comparison and pending-capture gate below.

Device construction `0x1800232a8` copies configuration byte `+0x434` into
device `+0x151`; Modern-Standby capability `0x1800e2120 == 1` together with
configuration byte `+0x442 == 1` forces that device byte to one. These are
configuration/capability predicates, not a pending capture pointer. Display
arming is owned by [the power callback](usbinterface-FUN_1800174a0.md).
The stored configuration defaults are `+0x434 = 0` at `0x18005e774` and
`+0x442 = 1` at `0x18005e782`; configuration loading can replace them.

In the normal image-mode branch the function:

1. Reads the FDT interrupt sample through callback `+0x70`.
2. Takes an immediate manual TX-off FDT reading through callback `+0x160` with
   argument zero.
3. Calls `FUN_180014c98` at `0x180014fb1` with 12 areas and threshold word
   `+0x31c`.

`FUN_180014c98` halves both 16-bit values and returns one only when every area's
absolute difference is at most the threshold.

Threshold word `+0x31c` is `delta_fdt`, derived by profile-9 sensor check
`FUN_180004a40` from OTP `diff = (otp[17] >> 1) & 0x1f`:

```text
diff == 0: delta_fdt = 0
diff != 0: scaled = ((diff + 5) * 0x32) >> 4
           delta_fdt = scaled / 5
```

The comparison is inclusive: an area is accepted when
`abs((a >> 1) - (b >> 1)) <= delta_fdt`; one area above the threshold makes the
event a real finger.

If the comparison returns one, the handler calls
`MilanHV_temperature_event` at `0x180014ffe`. That synchronously reacquires the
profile-9 FDT/TX-on/TX-off base set and sets the pending callback marker on
admission. After it returns, this down handler calls `+0xb0` with one and returns
without reading a live image.

The false/drift branch passes one to callback `+0xb0` regardless of whether
`MilanHV_temperature_event` restored `+0x232`. In particular, image-pair
validation failure can return zero from the refresh postlude while leaving
`+0x232` clear; the handler then returns zero and rearms FDT-down detection once.
It does not publish a live frame or invoke the live-frame completion callback.

If the comparison returns zero, the event is treated as a real finger. With a
valid image base and active capture callback, `FUN_1800150e0` reads the TX-on,
HV-enabled live frame using high DAC `+0x312` and dispatches it through the
registered completion callback.

The exact ordinary capture gate is sensor-mode dword `+0x1e0 == 0`, retained
image-valid byte `+0x237 == 1`, callback pointer `+0x240 != NULL`, and global
screen-active byte `0x18005f398 != 0`. FDT-base-valid byte `+0x232` is not the
image-valid operand of this gate. The separate MS image branch is outside the
standard request path.

The manual-FDT comparison and false-down refresh precede this live-image gate:
a null capture callback or mode 2 does not suppress those earlier operations.
There is also no wait-state `+0x1fc`, previous-frame/CPU-completion, or prior
release predicate before the comparison. A second down selected after successful
capture has cleared `+0x240` still reads manual FDT and can take false-down
refresh. If it is a genuine down, the null callback suppresses the image only,
and the ordinary postlude arms up. A down notification selected while wait state
is already up is not a protocol error in this handler.

On a genuine-down comparison with the live-image gate false, the handler still
calls `+0x110(1)` and **arms up** through `+0xb0(0)`. At
`0x180014f8e`, `ESI` retains the successful manual-FDT callback result; the
four failed-gate edges at `0x180015061..0x18001507d` join `0x180015089`
without replacing it. The `ESI != -1` postlude at `0x18001508b..0x18001509d`
therefore selects up, even with no capture callback, mode 2, or no valid image.
No live frame or DAC adjustment occurs on that route. This up arm supplies the
next lift event to request-independent maintenance; an inactive genuine down
is not an unconditional down rearm or a discarded notification.

Failure to obtain the raw event (`+0x70` nonzero) or manual TX-off sample
(`+0x160 == -1`) instead returns directly at `0x1800150ac`, without either
arm callback. This early failure differs from live-image failure, which reaches
the down rearm. Both leave the retained image and pending setup marker intact.

### Wake-On-Finger Read And DAC Side Effect

`MilanHV_ReadImg_ForWOF` allocates one `uint16(rows*columns*2)`-byte frame and
calls profile read callback `+0x158` at `0x1800155a4` with TX one, HV one,
DAC pointer `&HAL[0x312]`, adjustment one, finger-image one and mode four.
Neither the helper nor its caller's screen-off selector tests capture callback
`+0x240`, sensor mode or image-valid byte before this read. A null HAL or failed
temporary allocation returns `-1`; a read returning `-1` frees the temporary
without starting the later retained-frame handling.

After a non-`-1` read, the owner stops an existing timer at `+0x360`, calls
`0x180013e60`, then takes the retained-frame lock at `+0x368`. A zero read result
allocates the `+0x340` frame when absent, writes its byte size at `+0x34c`, and
copies the temporary into that retained frame. It frees the temporary and
returns the read status. The caller's action-`0x15` delivery still requires a
registered capture callback and retained frame; it is separate from acquisition.

Because the read enables `Milan_DynamicAdjustDac`, a successful wake-on-finger
read can update current high DAC/history even without an engine capture
callback. This is event-triggered acquisition, not periodic idle adjustment.
It does not replace the hardware reference. The current Linux driver has no
screen-off wake-on-finger capture owner; its adjustment-enabled reads are owned
by the active `device/scan.c:goodix_capture_ssm_handler`.

### Ordinary Request Read And Publication

`FUN_1800150e0` (`MilanHV_ReadImg`) derives one frame length as the low 16 bits
of `rows_u8_1f0 * columns_u8_1f1 * 2`; profile 9 uses `88 * 108 * 2 ==
0x4a40`. Capture count byte `+0x280` is one for identify/verify and two for
enrollment. Each callback `+0x158` call receives output, TX one, HV one,
`&context[0x312]`, adjust-DAC one, finger-image one, and capture mode four.

Profile-9 initializer `FUN_18000450c` installs `FUN_1800055d0` at `+0x158`.
That wrapper initializes a local status to zero and returns the status written
by `FUN_1800074bc`. The latter sends category two, command zero through
`FUN_180017ec0` with timeout argument 500. A false send/wait result or raw-read
status global `0x180060cc0 == -1` writes status `-1`, logged as
`read image timeout || read rawdata error`. This is the ordinary read-error
return consumed by `MilanHV_ReadImg`, not a completed-frame callback result.

Successful reads have a calibration side effect before loop continuation.
`FUN_1800074bc` copies the decoded frame to the caller and then calls
`FUN_180007c84` (`Milan_DynamicAdjustDac`) with the adjustment flag and the same
DAC pointer. Standard live capture passes adjustment one and `&HAL[0x312]`;
base acquisition passes adjustment zero. The enabled helper forms a register
word `(dac_h << 4) | 8`, calls `FUN_1800115a4` with the decoded live frame,
retained reference `+0x248`, profile dimensions, temperature scale and default
DAC, then writes `adjusted_word >> 4` back through the pointer. A zero
temperature word at `0x180060cb8` selects `0x80` before the left shift by four;
the default DAC is the word at `0x180060cbc`.

Consequently the second enrollment read uses the DAC produced by the first
successful read, and `CaptureFramedone` copies the DAC after the last successful
read into the sample trailer. A later read failure does not undo the earlier
DAC write or the adjustment helper's retained history. Discarding the first
live buffer therefore does not discard all effects of that first read. The
second live frame is not an algorithm image input, but its successful read can
change the metadata accompanying the first image and the next wire request.

The complete mask, mean, high/low/middle correction and retained-state contract
is owned by `usbinterface-FUN_180007c84.md`. It includes the native partial
stack-write dependency in the mask, the asymmetric downward correction limit,
and four module-static unsigned-16 history words. Sensor checking reseeds
temperature/default without clearing that history. The reference is the latest
admitted hardware base, including unmarked checkbase replacement, rather than
the older reference an initialized engine may retain for preprocessing.

This successful-read side effect maps to
`device/scan.c:goodix_capture_ssm_handler -> goodix_device_adjust_dac` in
`device/calibration.c`. Its reference is `FpiDeviceGoodix53x5.hardware_reference`,
and the next image command consumes the updated full `calib.dac_h` word.

From fresh zero history, temperature `0x80`, default/current DAC `0x97`, three
successful uniform `3201` live reads retain DAC `0x97`, `0x97`, then write
`0x98`. Uniform `3200` remains `0x97`. All such pixels unconditionally enter
the interior mean, so this result is independent of the mask's ambient stack
word. Algorithm sample-quality acceptance is not required for adjustment.

On each callback success, `FUN_1800150e0` increments the completed-frame count
and decrements `+0x280`. After all requested frames succeed, it calls the
registered `CaptureFramedone` callback at `+0x240` directly with device handle
`+0x00`, retained image base `+0x248`, temporary live buffer, total live byte
length, and one-shot byte `+0x236`. It then clears a nonzero marker, clears the
callback, frees the temporary live buffer, and returns zero. This direct call
is the ordinary standard-capture completion edge; device action `0x15` is a
separate completion route.

Those clears follow callback invocation even when cancellation prevents WDF
sample completion; the callback supplies no acknowledgement to this owner.
See `usbinterface-FUN_18001fb40.md#marker-consumption-is-callback-based`.

For a single-frame success, the callback observes remaining count zero but
still owns the nonnull HAL callback and the original one-shot marker. It
serializes and completes the pending standard request synchronously; only after
it returns does `MilanHV_ReadImg` clear marker/callback and free the live frame.
The later sensor-mode and up-arm callbacks therefore observe the cleared
callback and completed request. See `usbinterface-FUN_18001fb40.md` for the
complete sample-publication boundary. Neither success nor read failure changes
the retained reference pointer or its contents.

If callback `+0x158` returns `-1`, the function stops at that frame, frees the
entire temporary buffer, returns `-1`, and does not call or clear the completed-
frame callback. It does not clear the one-shot marker, retained image/reference
validity, retained image base, or the remaining capture count. A first-frame
failure therefore leaves the original count; a later-frame failure leaves the
unread remainder.

For an enrollment request starting at count two, first-read success followed by
second-read `-1` leaves count one. The successful first frame is freed with the
attempt buffer: no live frame is retained for concatenation with a later attempt.
On the next real-down invocation, `MilanHV_ReadImg` reads the remaining count at
`0x1800151d0`, allocates only `count * 0x4a40` bytes, and starts its local output
index at zero again. A successful retry therefore publishes the new single frame
with length `0x4a40`, the still-pending callback and marker, and the reference
current at that later invocation. With no intervening refresh the reference is
unchanged; an intervening admitted refresh can replace it and set the marker.
Repeated read failures retain count one. A first-read failure from count two
instead leaves two for the next invocation. A newly admitted capture request
sets its own count through `FUN_18000ebcc`; this remainder belongs to the pending
request, not to the next request.

The loop decrements on any return other than `-1`, while its publication gate
requires the final return to be exactly zero (`0x18001535a`). The installed
profile-9 read wrapper's ordinary results are zero and `-1`. All-two-success
publishes `0x9480` live bytes once; neither read individually completes a sample.
The sample consumer uses its first live frame for ordinary preprocessing; see
`FUN_18001f610.md` and `usbinterface-FUN_18001fb40.md`.

If all reads succeed but the callback is null or global image-initialized byte
`0x18005f398` is zero at the final publication gate, `FUN_1800150e0` restores
the original requested count and frees the temporary buffer without publication.
The ordinary admitted callback branch instead retains count zero. Image parser
readiness and authenticated-counter mutation precede this gate and can precede
the sender's ACK result; see
`usbinterface-FUN_180015c60.md#independent-image-reception`.

The down handler propagates the read-error `-1`, skips sensor-mode callback `+0x110`, and
calls arm callback `+0xb0(1)`. The registered callback and pending standard
request remain available to a later real-down event. On success, the handler
calls `+0x110(1)` and then `+0xb0(0)` to arm FDT-up.

For profile 9, `FUN_18000450c` installs `FUN_180005b70` at `+0x110`.
`FUN_180005b70` returns zero without mutation or transport activity; this call
does not add a sensor-mode command between publication and the up arm.

Callback `+0xb0` is profile-9 `FUN_180005a60`. The false/drift branch invokes
it with one to rearm FDT-down detection. The real-finger branch invokes it with
zero after capture to switch the sensor to FDT-up detection.

The arm callback's up branch sends category 3, command 2 with timeout 500 at
`0x180005af4`.
When the send/wait result's low two bits are both set, it calls
`FUN_1800059c0` with mode 4 and retries the same arm once. It then writes HAL
wait state `+0x1fc = 0xf1` at `0x180005b30` and returns zero at
`0x180005b3a` regardless of either transport
result. The down handler ignores the arm return in any case and returns its
earlier read/validation result. Thus an up-arm transport failure cannot retract
the already completed capture request, republish its frame, or restore its
cleared callback. Request cancellation has a separate owner; see
`usbinterface-FUN_18001fb40.md`.

The blocking controller worker `FUN_18000df20` treats handler return `-1` as a
logged event error only. It does not terminate the request or worker; after the
handler rearms down, the worker returns to its indefinite event wait.

## Current Source Mapping

`drivers/goodix53x5/device/scan.c:goodix_scan_coordinator_handler` owns the
down/manual-validation, capture-ready-before-up-arm and read-error down-rearm
boundaries. Its request-independent mode, entered through
`goodix_scan_start_service`, performs the same manual validation and false-down
refresh; genuine inactive down selects up arming without a live image.
`device/session.c:goodix_session_start_action` joins that selected handler before
foreground admission. `goodix_capture_ssm_handler` and `goodix_capture_ssm_done` implement
one live read per capture child. `device/enroll.c:goodix_enroll_capture_ready`
copies that frame into the enrollment worker input; the native count-two loop
and its retained unread-count state have no corresponding loop in that child.
The successful-read `FUN_1800074bc -> FUN_180007c84 -> FUN_1800115a4` DAC/history
mutation maps to `goodix_capture_ssm_handler` calling
`device/calibration.c:goodix_device_adjust_dac` after command success and raw
decode. `device/session.c:GOODIX_OPEN_PARSE_OTP` calls `goodix_device_parse_otp`
to seed current/default calibration. Capture owns module-static adjustment
history across device reconstruction.
`goodix_cmd_request_image` consumes the current full DAC word for the next wire
request; auth/enrollment runtime-input constructors consume the post-read word
for live metadata. The module history and independent hardware-reference
lifetime are mapped in
`usbinterface-FUN_180007c84.md`.

The current `GOODIX_SCAN_COORD_ARM_UP` state calls
`goodix_milan_generation_prepare_setup` only when `stop_requested` is false;
that preparation consumes `hardware_refresh_pending`. A successful selected
live read still adjusts DAC before reaching this gate. If cancellation has
requested stop, Linux skips both preparation and CPU delivery, retaining the
hardware marker. Native `FUN_1800150e0` instead invokes its installed callback
and consumes the marker even when `CaptureFramedone` finds no pending request.
This callback-based marker lifetime is separate from suppressing cancelled
application output.

Before manual validation, the current down dispatch tests `cycle_active`.
With an active cycle, an unreleased cycle whose wait mode is not down fails
with a protocol error; the other active-cycle route goes directly to recovery
up arming. Neither route executes `GOODIX_SCAN_COORD_DOWN_MANUAL` or its
false-down comparison. The normal inactive-service down route instead reaches
that comparison before suppressing live capture.
