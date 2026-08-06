# usbinterface.dll FUN_1800149c4

## Identity

- Address: `0x1800149c4`
- Logged name: `UP_Occure`
- Role: profile-9/type-12 finger-up handler that may refresh all FDT/image bases
  before the next probe.

## Profile-9 Dispatch

`FUN_1800162ac` installs `FUN_180015aa0` as the profile-9 up-event callback.
That wrapper calls this function at `0x180015abf`, performs the optional broken
sensor check, and then reports finger-up through callback slot `+0xb0`.

The MCU parser records an up IRQ as worker event `0x10`; the blocking controller
thread dispatches `FUN_180015aa0`, which runs this function synchronously. See
`usbinterface-profile9-fdt-event-loop.md` for the complete event path.

## Lift Comparison

When drift-anchor-empty byte `+0x338` is not one, this function reads the
current 12-area FDT values through callback `+0x70`, halves each value, and
compares them with the software drift anchor at `+0x320` through
`FUN_1800140e4` at `0x180014b1f`. The anchor is maintained by the reverse-event
path; it is distinct from the FDT base currently programmed into the sensor.

`FUN_1800140e4` counts areas whose absolute difference exceeds threshold word
`+0x318` and returns one only when more than half of the 12 areas exceed it. On
that result, `UP_Occure` clears base-valid byte `+0x232` and calls
`MilanHV_temperature_event` at `0x180014b6a`. A successful refresh clears the
old down base, sets `+0x338`, and leaves one-shot image-base refresh byte
`+0x236` for the next completed sample.

If the majority threshold does not fire, `FUN_180014030` performs the narrower
per-area `threshold / 3` check. It returns one only when every area differs by
strictly less than `delta_down / 3`; that result clears the anchor and sets
`+0x338` to one. Intermediate differences retain the active anchor. The
function then rearms the event path and calls `Milan_checkbase_isok`, which only
reacquires all bases if validity has already been lost.

The up wrapper subsequently calls profile-9 arm function `FUN_180005a60` with
one, returning the sensor to FDT-down detection before the controller thread
waits again.

## Retry Consequence

After a failed first authentication probe, normal finger lift reaches this
handler independently of the engine match result. If a software drift anchor is
active and the majority check fires, Windows refreshes the retained TX-on
reference and the next probe reinitializes preprocessing. Linux currently
transforms the lift event directly into its next FDT-down base but does not
maintain this separate reverse/up drift anchor and retains the same Milan
generation and TX-on reference. If the native check does not fire, both sides
continue with the open-time reference.

Profile setup does not explicitly initialize `+0x338`; reverse, up, and refresh
branches own its transitions. A fresh zero-initialized module begins with an
active zeroed anchor, but prior events and retained DLL state can change it.
Therefore the first up event must not be described as unconditionally refreshing
without observing the preceding reverse-event history.

Confidence: high for the comparison, refresh call, and one-shot handoff; the
real-world frequency of the threshold firing after a multi-hour wait is not yet
measured.
