# usbinterface.dll FUN_1800149c4

## Identity

- Address: `0x1800149c4`
- Logged name: `UP_Occure`
- Role: profile-9/type-12 finger-up handler that may refresh all FDT/image
  bases.

## Profile-9 Dispatch

`FUN_1800162ac` installs `FUN_180015aa0` as the profile-9 up-event callback.
That wrapper calls this function at `0x180015abf`, performs the optional broken
sensor check, and returns to the profile event path. The parser records an up
IRQ as worker event `0x10`; see
`usbinterface-profile9-fdt-event-loop.md` for the worker order.

## Anchor Decision

When anchor-empty byte `+0x338` is not one, callback `+0x70`
(`FUN_1800053f0`) supplies the current 12 raw event words. This function
normalizes each as unsigned `value >> 1` and compares the vector with anchor
`+0x320`:

1. `FUN_1800140e4` returns one when more than six areas differ by strictly more
   than `delta_down` at `+0x318`.
2. If no majority fires, `FUN_180014030` clears the anchor only when every area
   differs by strictly less than unsigned integer `delta_down / 3`. Equality
   retains it, as do other intermediate differences.

The reverse-event path owns anchor seeding; the anchor is distinct from the
sensor's programmed down base. `FUN_180004a40` owns `delta_down` derivation.

## Refresh And Rearm

On a majority, this function clears base-valid byte `+0x232` and synchronously
calls `MilanHV_temperature_event` at `0x180014b6a`. Successful refresh clears
the anchor, sets `+0x338`, and sets one-shot byte `+0x236` through the refresh
owner. Failed refresh retains the active anchor and leaves `+0x338` clear. Both
paths return before callback `+0x110` and `Milan_checkbase_isok`; wrapper
`FUN_180015aa0` still rearms FDT-down through `+0xb0`.

Without a majority, the function calls `+0x110` with zero and then
`Milan_checkbase_isok`, which reacquires all bases only when validity is already
lost. Refresh ownership and failure effects are documented in
`usbinterface-FUN_180013da4.md` and `usbinterface-FUN_180015c60.md`.

Profile setup does not explicitly initialize `+0x338`; reverse, up, and refresh
branches own its transitions. A fresh zero-initialized module begins with an
active zeroed anchor, but prior events and retained DLL state can change it.
An up event therefore refreshes from this path only when preceding reverse-event
history has left an active anchor and the strict majority predicate fires.
