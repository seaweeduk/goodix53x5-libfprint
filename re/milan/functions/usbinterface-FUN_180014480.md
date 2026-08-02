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

## Consequence

This is event-driven recalibration within one attached hardware context. It is
not tied to an enrollment or identify operation boundary.
