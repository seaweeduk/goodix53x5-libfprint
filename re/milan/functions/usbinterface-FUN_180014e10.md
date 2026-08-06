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
3. Calls `FUN_180014c98` at `0x180014fc2` with 12 areas and threshold word
   `+0x31c`.

`FUN_180014c98` halves both 16-bit values and returns one only when every area's
absolute difference is at most the threshold. This is the native false-event
shape mirrored by Linux's `goodix_device_is_fdt_base_valid` check.

Threshold word `+0x31c` is `delta_fdt`, derived by profile-9 sensor check
`FUN_180004a40` from OTP `diff = (otp[17] >> 1) & 0x1f`:

```text
diff == 0: delta_fdt = 0
diff != 0: scaled = ((diff + 5) * 0x32) >> 4
           delta_fdt = scaled / 5
```

The comparison is inclusive: an area is accepted when `abs(a >> 1 - b >> 1)
<= delta_fdt`; one area above the threshold makes the event a real finger.

If the comparison returns one, the handler calls
`MilanHV_temperature_event` at `0x180014ffe`. That synchronously reacquires the
profile-9 FDT/TX-on/TX-off base set, marks the next completed sample on success,
reports the event as non-capturing through callback `+0xb0`, and returns without
reading a live image.

If the comparison returns zero, the event is treated as a real finger. With a
valid image base and active capture callback, `FUN_1800150e0` reads the TX-on,
HV-enabled live frame using high DAC `+0x312` and dispatches it through the
registered completion callback.

Callback `+0xb0` is profile-9 `FUN_180005a60`. The false/drift branch invokes
it with one to rearm FDT-down detection. The real-finger branch invokes it with
zero after capture to switch the sensor to FDT-up detection.

## Long-Wait Consequence

Windows does not refresh merely because an operation has waited for a long
time. It refreshes before the first real probe only if idle drift/noise first
produces this false-event comparison. Linux recognizes and rearms the false
event but currently retains its open-time TX-on reference, which is a static
parity divergence for long-lived claims.

Confidence: high from the profile-9 callback assignment, decompiled branch
ordering, helper arithmetic, and the separate live-image call.
