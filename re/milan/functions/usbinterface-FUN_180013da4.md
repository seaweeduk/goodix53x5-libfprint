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

If refresh fails, `+0x232` remains clear and `+0x236` is not set. The caller's
event wrapper still rearms detection; later base-valid checks can retry. The old
image-base-valid byte and retained image storage are not discarded merely
because this refresh attempt failed.

## Interpretation

Despite its logged name, no OS thermal-notification registration is proven.
The known callers are FDT down/up/reverse handlers reacting to sensor-base
comparisons.

For profile 9 / sensor type 12, the two authentication-relevant routes are:

- `MilanHV_Down_procedure` calls this function when the FDT interrupt sample and
  an immediate manual TX-off reading remain within the configured per-area
  threshold. Native treats that as drift/noise rather than a real finger,
  refreshes all FDT/image bases, and does not capture a live image for that
  event.
- `UP_Occure` calls this function after lift when more than half of the 12 FDT
  areas differ from the active software drift anchor by more than the
  configured down threshold. That anchor is maintained by reverse events and is
  distinct from the FDT-down base programmed into the sensor. A successful
  refresh therefore marks the next probe for preprocessing reinitialization.

## Linux Parity Status

The Linux driver captures a fresh FDT/image reference set on every device open.
During a long wait it detects the same false FDT-down shape, but only rearms the
wait; it does not run the native full-base refresh. After a failed probe it
replaces its FDT-down base from the lift event, but again does not refresh the
TX-on image reference. Native can refresh at either boundary and sends a
one-shot marker that reinitializes preprocessing on the next completed sample.

Consequently, parity is conditional. If neither native drift comparison fires,
both first probes use their open-time TX-on reference and both second probes
continue using it. If native detects idle drift before the first real touch, or
detects drift on lift after a failed first touch, Windows refreshes while Linux
keeps the old image reference. No multi-hour lock failure or trigger frequency
has yet been measured on the profile-9/type-12 sensor.

The static divergence is proven; its practical effect remains unvalidated.
Validation should use source/DLL-level fixtures and existing diagnostic frame
dumps rather than assuming that elapsed time alone triggers the native branch.
