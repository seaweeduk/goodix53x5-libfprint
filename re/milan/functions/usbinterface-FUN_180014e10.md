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
profile-9 FDT/TX-on/TX-off base set and marks the next completed sample on
success. After it returns, this down handler calls `+0xb0` with one and returns
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
image-initialized byte `0x18005f398 != 0`. FDT-base-valid byte `+0x232` is not the
image-valid operand of this gate. The separate MS image branch is outside the
standard single-frame request path.

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

On each callback success, `FUN_1800150e0` increments the completed-frame count
and decrements `+0x280`. After all requested frames succeed, it calls the
registered `CaptureFramedone` callback at `+0x240` directly with device handle
`+0x00`, retained image base `+0x248`, temporary live buffer, total live byte
length, and one-shot byte `+0x236`. It then clears a nonzero marker, clears the
callback, frees the temporary live buffer, and returns zero. This direct call
is the ordinary standard-capture completion edge; device action `0x15` is a
separate completion route.

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

The down handler propagates this `-1`, skips sensor-mode callback `+0x110`, and
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
