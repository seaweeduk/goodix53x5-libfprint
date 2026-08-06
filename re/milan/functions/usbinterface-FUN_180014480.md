# usbinterface.dll FUN_180014480

## Identity

- Address: `0x180014480`
- Logged name: `Reverse_Occure`
- Role: HV reverse/FDT event handler that validates retained FDT state and may
  trigger image-base refresh.

## Image-Base Behavior

- If base-valid byte `+0x232` is not one at entry, directly calls
  `MilanHV_update_allbase` at `0x1800144cd`.
- With a valid base, compares live FDT values against the retained down base.
- Drift branches clear `+0x232` and call `MilanHV_temperature_event` at
  `0x180014698` or `0x18001480d`.
- Successful temperature refresh eventually sets one-shot byte `+0x236`; this
  function itself does not set or consume that marker.

## Reverse-Event Drift State

For profile 9, MCU parser `FUN_180005b80` classifies IRQ `0x80` or `0x82` as a
reverse event. Before waking the controller worker it snapshots the prior
hardware FDT-down arm base, transforms the current raw event into the next
hardware down-arm base, and selects this reverse callback.

This function normalizes the current 12 raw areas with `value >> 1`. Callback
`+0x170` (`FUN_1800053b0`) supplies the prior hardware arm base as its 12
high-byte normalized values. It first calls `FUN_1800140e4` to count areas whose
absolute difference is greater than `delta_down` (`+0x318`):

- More than six changed areas triggers full temperature/base refresh.
- Otherwise, if drift-anchor-empty byte `+0x338` is one, the current normalized
  vector is saved at `+0x320` and `+0x338` is cleared.
- With an active anchor, a second majority comparison between the anchor and
  current vector catches cumulative drift and triggers full refresh.
- If no majority fires and every anchor difference is strictly less than
  `delta_down / 3`, the anchor is cleared and `+0x338` is set to one.
- Intermediate differences retain the anchor for a later reverse or up event.

A successful full refresh zeroes the anchor and sets `+0x338` to one. The
reverse wrapper then rearms FDT-down detection.

`delta_down` is derived by `FUN_180004a40` from the same OTP difference used by
`delta_fdt`: it is `0x0d` when OTP difference is zero, otherwise
`(((diff + 5) * 0x32) >> 4) / 3`.

## Consequence

This is event-driven recalibration within one attached hardware context. It is
not tied to an enrollment or identify operation boundary.

Profile-9 initialization in `FUN_1800162ac` installs `FUN_180015a60` as the
reverse-event callback; that wrapper calls this function and then reports the
event through callback slot `+0xb0`. This establishes the route for sensor type
12 without relying on branches for other profiles.
