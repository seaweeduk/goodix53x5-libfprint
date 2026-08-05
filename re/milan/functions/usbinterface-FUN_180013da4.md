# usbinterface.dll FUN_180013da4

## Identity

- Address: `0x180013da4`
- Logged name: `MilanHV_temperature_event`
- Role: invalidate and synchronously refresh the HV FDT/image-base set after an
  FDT comparison indicates environmental drift.

## Findings

- Clears base-valid byte `+0x232` at `0x180013dfa`.
- Calls `MilanHV_update_allbase` at `0x180013e01`.
- If `+0x232` is one after the call, sets one-shot preprocessor-refresh byte
  `+0x236` at `0x180013e11`.
- It does not clear image-base-valid byte `+0x237` before attempting refresh.
- Direct callers are `Reverse_Occure` at `0x180014698` and `0x18001480d`,
  `UP_Occure` at `0x180014b6a`, and `MilanHV_Down_procedure` at
  `0x180014ffe`.

## Handoff

The next completed sample carries `+0x236` to `CaptureFramedone` and then to
`GoodixEngineAdapter.dll`. `FUN_18000e1f0` clears the marker at
`0x18000e661` after callback dispatch, making preprocessing reinitialization a
one-sample notification rather than a per-probe operation.

## Interpretation

Despite its logged name, no OS thermal-notification registration is proven.
The known callers are FDT down/up/reverse handlers reacting to sensor-base
comparisons.

## Linux Parity Status

The Linux driver captures a fresh FDT/image reference set on every device open
and updates its FDT-down base after finger lift. It does not implement this
native event-driven full-base refresh within a long-lived authentication claim.
This can leave the original image reference in use while a lock screen waits
for an extended period, although no multi-hour lock failure has been observed
on the profile-9/type-12 sensor.

Native-style event counting and synchronous `update_allbase` refresh are
deferred unless runtime captures show baseline drift, elevated MAD, or degraded
first-touch matching during a long-lived claim.
