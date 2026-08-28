# Profile-9 Reference Refresh Across Authentication Attempts

## Scope

This note describes the profile-9 / sensor-type-12 native reference lifetime
across initialization, FDT events, completed samples, and authentication
operation boundaries. Elapsed time is not a refresh trigger.

## Initial Acquisition

Full device initialization invokes device action `0x0c`, which dispatches
`MilanHV_update_allbase`. The function captures TX-on and TX-off image bases and
retains the TX-on image only if all of these predicates admit the acquisition:

- The first TX-on and TX-off FDT samples pass `FUN_180014c98`.
- The TX-on/TX-off image pair passes `FUN_180014ce8`.
- The second TX-on FDT sample passes `FUN_180014c98` against the TX-off sample.

Complete admission sets `+0x232/+0x233/+0x237` and makes the retained TX-on
image at `+0x248` available to later samples. Image-pair rejection or final
TX-on FDT rejection does not create a valid image reference: initial profile
setup has cleared `+0x237`, and the common postlude leaves `+0x248/+0x237`
unchanged. The final manual response's touch flag is not retained.

Either rejection still transforms the first TX-on FDT sample and programs the
primary, down-arm, up-arm, and manual-FDT bases. Action `0x0c` can return zero
through the common postlude even though the image reference remains invalid;
it does not arm detection, and `deviceInit` does not branch on that action
return.

## Operation Start And Retry

Device action `3` invokes `FUN_180015710`, which requests mode `4` and arms
FDT-down detection. This operation-start path does not require `+0x232` or
`+0x237` to be set.

The ordinary `OnCaptureData` path also selects FDT-down for its first activation
and retains the WBF request while image validity remains clear. A real down
event cannot publish an image in that state; the down handler switches to
FDT-up and retains the request. On the corresponding up event, a fresh zero
anchor state reaches `MilanHV_temperature_event` through the majority-change
predicate; other anchor states can reach the final `Milan_checkbase_isok` call.
Both routes rerun `MilanHV_update_allbase` while `+0x232` is clear. Admission
establishes the first valid reference, the up wrapper rearms down, and a later
real down can complete the same retained WBF request. Only the temperature-event
route writes one-shot byte `+0x236`.

If a later false-down event reaches `MilanHV_Down_procedure`, the handler calls
`MilanHV_temperature_event`. That function clears `+0x232` and reruns
`MilanHV_update_allbase`. If every acquisition predicate admits the refresh,
the refresh replaces the retained TX-on image, restores the validity bytes, and
sets one-shot byte `+0x236`. The down handler then rearms FDT-down detection.

If the refresh is rejected, the common FDT postlude still runs, but the prior
`+0x248/+0x237` state remains unchanged. Neither `MilanHV_update_allbase` nor
`MilanHV_temperature_event` writes `+0x236` on validation rejection; an earlier
unconsumed marker value is unchanged. The down handler still rearms detection.
A later qualifying FDT event can reach another base-validity check and retry
acquisition.

## Lift And Reverse Events

The up and reverse paths maintain a software drift anchor distinct from the
FDT base programmed into the sensor. Their documented predicates can call
`MilanHV_temperature_event` or `MilanHV_update_allbase` when refresh is needed.
Each owning event wrapper performs the subsequent down rearm.

Any admitted update replaces the retained image after all pair and FDT
predicates pass. Only routes through `MilanHV_temperature_event` set the
one-shot marker; direct update callers do not. A rejected refresh neither
replaces the retained image nor writes the marker.

## Completed-Sample Handoff

Capture completion passes retained image reference `+0x248`, the live image,
and one-shot marker `+0x236` to `CaptureFramedone`. The dispatcher clears
`+0x236` after that callback. When `MilanHV_temperature_event` writes the marker,
it requests preprocessing reinitialization for exactly one completed sample.

Normal operation clearing does not itself reacquire the image base. In the
absence of an admitted initial acquisition or an admitted event-driven refresh,
there is no valid retained image reference to hand off.

## Authorities

- Base acquisition and validation-failure postlude:
  `usbinterface-FUN_180015c60.md`
- Initial callback and validity setup: `usbinterface-FUN_1800162ac.md`
- Device action dispatch: `usbinterface-FUN_18000e1f0.md`
- FDT scheduler and wrapper rearm ownership:
  `usbinterface-profile9-fdt-event-loop.md`
- Down-event decision: `usbinterface-FUN_180014e10.md`
- Up-event decision: `usbinterface-FUN_1800149c4.md`
- Reverse-event decision: `usbinterface-FUN_180014480.md`
- One-shot engine decision: `FUN_18001f610.md`
- Engine operation clear: `FUN_18001f090.md`
