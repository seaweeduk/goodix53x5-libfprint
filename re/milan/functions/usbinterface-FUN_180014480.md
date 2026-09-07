# usbinterface.dll FUN_180014480

## Identity

- Address: `0x180014480`
- Logged name: `Reverse_Occure`
- Role: HV reverse/FDT event handler that validates retained FDT state and may
  trigger image-base refresh.

## Dispatch And Inputs

For profile 9, parser `FUN_180005b80` classifies IRQ `0x80` and `0x82` as
reverse events. `FUN_1800162ac` installs wrapper `FUN_180015a60`, which calls
this function. See `usbinterface-profile9-fdt-event-loop.md` for parser, worker,
and wrapper order.

Callback `+0x70` (`FUN_1800053f0`) returns the current 12 raw event words;
this function normalizes each as unsigned `value >> 1`. Callback `+0x170`
(`FUN_1800053b0`) returns the high byte of each prior down-arm word as an
unsigned 16-bit value. The parser preserves that prior base before installing
the transformed current event; see `usbinterface-FUN_180005420.md` for the
retained manual-store relationship.

## Anchor Decision

If base-valid byte `+0x232` is not one, the function calls
`MilanHV_update_allbase` directly at `0x1800144cd`. Otherwise it applies these
predicates using `delta_down` at `+0x318`:

1. `FUN_1800140e4` counts areas where the current normalized value differs from
   the prior down-arm value by strictly more than `delta_down`. More than six
   areas triggers refresh.
2. If that comparison does not trigger and anchor-empty byte `+0x338` is one,
   the current vector is copied to anchor `+0x320` and `+0x338` is cleared.
3. With an active anchor, the same strict majority comparison between the
   anchor and current vector triggers refresh on more than six changed areas.
4. Otherwise `FUN_180014030` clears the anchor only when every difference is
   strictly less than unsigned integer `delta_down / 3`. Equality retains it,
   as do other intermediate differences.

`FUN_180004a40` owns `delta_down` derivation.

The prior-arm majority at `0x18001463e..0x180014698` short-circuits all anchor
work, including seeding. The empty-anchor branch at
`0x18001491a..0x180014981` copies all 24 normalized bytes and goes directly to
callback `+0x110`; it does not compare the newly seeded anchor on the same
event. An active anchor is retained unchanged for intermediate differences,
not moved toward the current vector. Only a subsequent qualifying event clears
or refreshes it. Neither anchor seeding nor proximity clearing writes the
programmed down base or the separately retained manual base.

Both comparison helpers zero-extend their 16-bit operands before subtracting
in 32 bits. For the fixed twelve-area call, the changed count cannot wrap;
exactly six changed areas and differences equal to `delta_down` do not trigger.
The proximity helper computes unsigned integer division by three before its
comparison, so a zero quotient cannot clear even an identical anchor.

## Refresh And Rearm

The two drift branches clear `+0x232` and synchronously call
`MilanHV_temperature_event` at `0x180014698` or `0x18001480d`. Successful
refresh zeroes the anchor, sets `+0x338`, and arranges the one-shot sample marker
through the refresh owner. Failed refresh leaves the anchor and `+0x338`
unchanged. See `usbinterface-FUN_180013da4.md` and
`usbinterface-FUN_180015c60.md` for refresh ownership and failure effects.

On a no-refresh path this function calls callback `+0x110` with zero before
returning to `FUN_180015a60`; the wrapper then rearms FDT-down through `+0xb0`.
The wrapper supplies a byte argument (`CL = 1` at `0x180015a83`) and tail-calls
the arm callback, returning its result rather than the handler's result.
