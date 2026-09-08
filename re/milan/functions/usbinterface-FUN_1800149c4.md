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

An empty anchor bypasses the raw-event callback and both comparisons at
`0x180014a4a..0x180014a51`; up never seeds it. With an active anchor, the
comparison and any proximity clearing happen before the base-valid check in
`Milan_checkbase_isok`, regardless of the entry value of `+0x232`. Intermediate
differences leave all 24 anchor bytes unchanged. The proximity-clear writes at
`0x180014c13..0x180014c29` zero all twelve words and set the empty byte; they do
not update the programmed down base or retained manual base.

## Refresh And Rearm

On a majority, this function clears base-valid byte `+0x232` and synchronously
calls `MilanHV_temperature_event` at `0x180014b6a`. Successful refresh clears
the anchor, sets `+0x338`, and sets one-shot byte `+0x236` through the refresh
owner. Failed refresh retains the active anchor and leaves `+0x338` clear. Both
paths return before callback `+0x110` and `Milan_checkbase_isok`; wrapper
`FUN_180015aa0` still rearms FDT-down through `+0xb0`.
The rearm argument is a byte (`CL = 1` at `0x180015b7c`). The wrapper saves the
callback's 32-bit result at `0x180015b8b` and returns it after any retained-frame
cleanup, independently of the handler's result.

Without a majority, the function calls `+0x110` with zero and then
`Milan_checkbase_isok`, which reacquires all bases only when validity is already
lost. Refresh ownership and failure effects are documented in
`usbinterface-FUN_180013da4.md` and `usbinterface-FUN_180015c60.md`.

Profile setup does not explicitly initialize `+0x338`; reverse, up, and refresh
branches own its transitions. A fresh zero-initialized module begins with an
active zeroed anchor, but prior events and retained DLL state can change it.
The majority refresh path therefore requires an active anchor and the strict
majority predicate, not proof that a reverse event has previously seeded it.
Ordinary reverse seeding is one producer of that active state. The non-majority
path separately checks base validity after callback `+0x110` at
`0x180014c55..0x180014c67`; this caller performs no additional anchor clearing
after `Milan_checkbase_isok` returns.

`Milan_checkbase_isok` (`FUN_1800141f0`) calls `MilanHV_update_allbase` only
when `+0x232` is zero. That acquisition owner does not clear the software
anchor either. An invalid-base up event with an active intermediate-distance
anchor therefore retains all twelve anchor words and the active flag through
successful reference/FDT-base publication and the wrapper's down rearm. A
proximity-qualified up event clears the anchor before the same recovery call;
a majority-qualified up event instead uses the temperature route and clears
only after successful acquisition.

An ordinary validation rejection during a preceding majority refresh can leave
`+0x232` clear, the active anchor intact, and an older admitted image at `+0x248`
with `+0x237` still set. `MilanHV_Down_procedure` (`FUN_180014e10`) does not call
`Milan_checkbase_isok` on its genuine-down branch: after its FDT comparison it
gates live-image acquisition on image validity and capture ownership/enabling,
not FDT-base validity. The older image-valid state therefore does not imply
that the next up event enters with `+0x232` set.
