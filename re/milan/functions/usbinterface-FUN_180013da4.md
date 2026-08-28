# usbinterface.dll FUN_180013da4

## Identity

- Address: `0x180013da4`
- Logged name: `MilanHV_temperature_event`
- Role: invalidate the FDT base and synchronously attempt full FDT/image-base
  acquisition after an FDT comparison indicates environmental drift.

## Findings

- Clears base-valid byte `+0x232` at `0x180013dfa`.
- Calls `MilanHV_update_allbase` at `0x180013e01`.
- If `+0x232` is one after the call, sets one-shot preprocessor-refresh byte
  `+0x236` at `0x180013e11`.
- It does not clear image-base-valid byte `+0x237` before attempting refresh.
- Direct callers are `Reverse_Occure` at `0x180014698` and `0x18001480d`,
  `UP_Occure` at `0x180014b6a`, and `MilanHV_Down_procedure` at
  `0x180014ffe`.
- Returns the `MilanHV_update_allbase` status unchanged. Success of the refresh
  itself is owned separately by the post-call `+0x232 == 1` test; an image-pair
  or final TX-on FDT rejection can therefore return zero while leaving `+0x232`
  clear and without writing `+0x236`.

## Handoff

The next completed sample carries `+0x236` to `CaptureFramedone` and then to
`GoodixEngineAdapter.dll`. `FUN_18000e1f0` clears the marker at
`0x18000e661` after callback dispatch, making preprocessing reinitialization a
one-sample notification rather than a per-probe operation.

On image-pair or final TX-on FDT rejection, `+0x232` remains clear and this
function does not write `+0x236`; an earlier unconsumed marker value is
unchanged. The caller's down handler or up/reverse wrapper owns the subsequent
rearm; this function does not invoke the arm callback. Later base-valid checks
can retry. Image-valid byte `+0x237` and retained image storage `+0x248` remain
unchanged, whether they previously represented a valid image or no valid image.
The common postlude still updates retained and programmed FDT bases.

## Interpretation

The known callers are FDT down/up/reverse handlers reacting to sensor-base
comparisons.

For profile 9 / sensor type 12, the two authentication-relevant routes are:

- `MilanHV_Down_procedure` calls this function when the FDT interrupt sample and
  an immediate manual TX-off reading remain within the configured per-area
  threshold. The handler treats that as drift/noise rather than a real finger,
  attempts full-base acquisition, and does not capture a live image for that
  event. Only an admitted acquisition replaces the retained image reference.
- `UP_Occure` calls this function after lift when more than half of the 12 FDT
  areas differ from the active software drift anchor by more than the
  configured down threshold. That anchor is maintained by reverse events and is
  distinct from the FDT-down base programmed into the sensor. An admitted
  refresh replaces the image reference and writes the one-shot marker.
