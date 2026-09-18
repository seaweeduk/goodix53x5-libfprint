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

The full initialization worker `FUN_180020970` checks the device-active global
`DAT_1800600a4 != 0` and device-context stop byte `+0x68e0 != 1` around actions
`0x0a` and `0x0c`. With those gates retained, it does not branch on action
`0x0c`'s return: even mode-4/first-manual failure continues to the final firmware
query, mode-2 sleep with argument word 200, initialized-byte publication at
outer context `+0x110`, and initialization-event signalling. A later image/FDT
read failure already returned zero through the concrete acquisition postlude.
These paths do not request EC control or synchronously repeat acquisition.

## Operation Start And Retry

Device action `3` invokes `FUN_180015710`, which requests mode `4` and arms
FDT-down detection. This operation-start path does not require `+0x232` or
`+0x237` to be set.

The ordinary `OnCaptureData` path also selects FDT-down for its first activation
and retains the WBF request while image validity remains clear. A real down
event cannot publish an image in that state; the down handler switches to
FDT-up and retains the request. Profile initialization `FUN_1800162ac` sets
anchor-empty byte `+0x338` to one at `0x1800164b0`, leaving the twelve anchor
words unchanged but inactive. Without intervening reverse-event seeding, the
corresponding up event skips both anchor comparisons and reaches
`Milan_checkbase_isok`. That helper directly calls `MilanHV_update_allbase` when
`+0x232` is clear. With an active anchor, a strict majority-change predicate
instead selects `MilanHV_temperature_event`; a non-majority path still reaches
the validity check after any proximity-qualified anchor clearing.

Admission establishes the first valid reference, the up wrapper rearms down,
and a later real down can complete the same retained WBF request. Only the
temperature-event route writes one-shot byte `+0x236`. Initial direct recovery
needs no marker to initialize a fresh engine context: its initialized byte
`+0x7c` is zero.

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

False-down refresh preserves the software anchor even on complete admission.
The ordinary branch at `014e10:0x180014ff6..0x180015007` calls `013da4` and
jumps directly to down rearm at `0x18001509f`, with no `+0x320..+0x338`
mutation. `013da4` clears only base validity, calls `015c60`, and conditionally
sets the marker; `015c60` does not own the anchor either. The down parser
`005b80` updates the current event and up-arm base without clearing the anchor.
An active vector previously seeded by a reverse event therefore survives a
subsequent false-down acquisition and remains input to the next up/reverse
majority decision. Reverse- and up-majority callers explicitly clear that vector
after admission; they have different caller-owned postludes.

## Lift And Reverse Events

The up and reverse paths maintain a software drift anchor distinct from the
FDT base programmed into the sensor. Their documented predicates can call
`MilanHV_temperature_event` or `MilanHV_update_allbase` when refresh is needed.
Each owning event wrapper performs the subsequent down rearm.

Up/reverse post-refresh anchor decisions consume the resulting validity byte
rather than the acquisition return. `UP_Occure` and
`Reverse_Occure` return zero for a non-null context, including a configuration
failure. Their wrappers call the down-arm slot afterward. On reverse **drift**
failure, the handler additionally restores only the down base from its saved
triggering event after any common acquisition postlude. Direct reverse-invalid
recovery and up handlers do not perform that restoration. Thus a rejected drift
refresh can leave down based on the event while up/manual/retained FDT storage
hold the first manual TX-on sample; see `usbinterface-FUN_180014480.md`.

Any admitted update replaces the retained image after all pair and FDT
predicates pass. Only routes through `MilanHV_temperature_event` set the
one-shot marker; direct update callers do not. A rejected refresh neither
replaces the retained image nor writes the marker.

## Completed-Sample Handoff

Capture completion passes retained image reference `+0x248`, the live image,
and one-shot marker `+0x236` to `CaptureFramedone`. The dispatcher clears
`+0x236` after that callback. When `MilanHV_temperature_event` writes the marker,
it requests preprocessing reinitialization for exactly one completed sample.

Engine setup uses the predicate `context[+0x7c] == 0 || marker == 1`. Thus a
direct invalid-base recovery can replace the hardware reference while an
initialized engine retains its existing preprocessing workspace and consumed
setup. For example, a rejected temperature refresh can leave FDT validity clear
and an older image valid; a later non-majority up recovery replaces the image
without setting the marker. An earlier unconsumed marker remains unchanged by
that direct recovery, so a marker already equal to one still requests setup
from the latest admitted reference.

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

## Current Source Map

`usbinterface.dll:0x180020970` initial acquisition maps to
`drivers/goodix53x5/device/session.c:goodix_open_ssm_handler` and the nonforced
`device/base.c` state machine. Event acquisition maps to
`device/scan.c:goodix_scan_coordinator_handler` plus the forced base child.
The six current reason values correspond to:

| Current reason | Native entry/decision | Admitted marker and anchor |
| --- | --- | --- |
| `NONE` | Initial action `0x0c` → `015c60` | No marker write; initial anchor is inactive. |
| `FALSE_DOWN` | `014e10` → `013da4` → `015c60` | Set marker; caller does not clear the software anchor. |
| `REVERSE` | Either majority in `014480` → `013da4` | Set marker and clear anchor when validity is restored. |
| `UP` | Active-anchor majority in `0149c4` → `013da4` | Set marker and clear anchor when validity is restored. |
| `INVALID_BASE` | Reverse invalid entry in `014480` → `015c60` | No marker write; clear anchor only after admission. |
| `UP_INVALID_BASE` | Non-majority `0149c4` → `0141f0` → `015c60` | No marker write; retain the anchor after any earlier proximity clear. |

Generation/process/setup-marker handling maps to
`device/base.c:goodix_milan_generation_retain_process`,
`goodix_milan_generation_prepare_setup`, and `goodix_base_ssm_handler`, with
sample-time setup admission in `milan/runtime.h:goodix_milan_runtime_initialize_setup`.
Sleep/error/reinit continuations are owned by `device/session.c` and
`device/scan.c`; this acquisition owner never clears algorithm process state.
