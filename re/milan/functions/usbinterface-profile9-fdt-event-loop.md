# usbinterface.dll Profile-9 FDT Event Loop

## Scope

This note covers sensor type 12, which selects profile 9. It documents the
scheduling and dispatch path around `MilanHV_Down_procedure`, `UP_Occure`, and
`Reverse_Occure` threshold checks.

## Worker Lifetime

`device_enable` (`FUN_18000e9b0`) selects profile 9 and calls
`FUN_1800162ac`. After profile setup it calls `FUN_18000e138`, which creates one
`FUN_18000df20` controller thread suspended and then resumes it.

`FUN_18000df20` is a blocking event worker, not a periodic recalibration task.
Its main loop waits indefinitely on the profile context event at `+0x10`, resets
that event after wake, reads the event type at `+0x08`, dispatches one handler,
and waits again. There is no timeout, elapsed-time comparison, or temperature
read in this loop.

## Profile-9 Event Source

`FUN_18000450c` (`GxFNHV_MilanOpen`) installs `FUN_180005b80`
(`milanget_fdtdata`) at profile callback slot `+0x148`. This is the sensor type
12 MCU FDT-packet parser. Relevant IRQ values are:

| IRQ value | Parser action | Worker event |
| --- | --- | --- |
| `0x0002` | Save down-event touch flag and 12 FDT areas; calculate the FDT-up arm base | `0x0f` |
| `0x0200` | Save up-event data; transform it into the next FDT-down arm base | `0x10` |
| `0x0080` or `0x0082` | Save reverse-event data and update the FDT-down arm base | `0x11` |

After storing the event type at profile context `+0x08`, the parser calls
`SetEvent(profile_context->event_10)`. A manual-FDT response uses a separate
completion event and does not enter this state-machine dispatch.

For each down, up, or reverse event, the parser copies the 24 FDT sample bytes
after the IRQ/touch header as 12 little-endian words without masking or
rejecting a sample value. Only the up and reverse event routes call
`FUN_180005538`; it transforms a private copy and replaces the down-arm base.
See `usbinterface-FUN_1800048d0.md` for the equivalent transform contract.

The raw event copy remains at `0x180060760` after that transform.
`FUN_1800053f0`, installed at callback slot `+0x70`, returns those exact 24 raw
bytes to the reverse and up handlers. On a reverse IRQ the parser first copies
the prior down-arm base to `0x180060778`; callback `+0x170`
(`FUN_1800053b0`) later returns its 12 high bytes as the prior normalized arm
values. Thus parser ordering preserves both operands while installing the new
hardware down base before worker dispatch. The reverse snapshot also remains
the manual-FDT base until another owner replaces it; see
`usbinterface-FUN_180005420.md`.

## Dispatch

`FUN_18000df20` maps the profile-9 event types through `FUN_18000e1f0`:

| Worker event | Device action | Profile callback |
| --- | --- | --- |
| `0x0f` | `0` | slot `+0x190`: `FUN_180014e10` (`MilanHV_Down_procedure`) |
| `0x10` | `1` | slot `+0x188`: `FUN_180015aa0` (`MilanHV_UP_procedure`) |
| `0x11` | `4` | slot `+0x198`: `FUN_180015a60` (reverse wrapper) |

`FUN_1800162ac` installs those three profile-9 callbacks. The up and reverse
wrappers call `UP_Occure` and `Reverse_Occure` respectively before rearming.

The worker executes the selected handler synchronously. Any
`MilanHV_temperature_event` / `MilanHV_update_allbase` call therefore completes
before control returns to the handler or wrapper. On reverse/up no-refresh paths
the handler calls profile callback `+0x110` with zero before its wrapper calls
arm callback `+0xb0` with one. Refresh branches may return before that handler
postlude, but the up/reverse wrapper still performs the down rearm. The down
handler owns its rearm directly. Refresh state and failure effects are owned by
`usbinterface-FUN_180013da4.md` and `usbinterface-FUN_180015c60.md`.

Each refresh branch attempts full-base acquisition. Complete admission replaces
the retained image and image-valid state. Image-pair rejection still runs the
common FDT postlude, replacing retained FDT-calibration storage and programmed
FDT bases while leaving retained image state unchanged. On a temperature-event
rejection, `+0x232` remains clear and `+0x236` is not written; any earlier
unconsumed marker value is unchanged.

## Hardware Arming And Rearming

`FUN_18000450c` installs `FUN_180005a60` at profile slot `+0xb0`:

- Argument `1` issues the profile-9 FDT-down detect command using the retained
  down-arm base and records wait state `0xf0`.
- Argument `0` issues the FDT-up detect command using the calculated up-arm base
  and records wait state `0xf1`.

The resulting state transitions are:

1. A capture request arms FDT-down detection and leaves the worker asleep.
2. A down IRQ wakes the worker and runs `MilanHV_Down_procedure`.
3. A false/drift down event synchronously attempts full-base acquisition and
   rearms down; no live image is captured. Admission replaces the retained image,
   while pair rejection updates only the FDT stores.
4. A real down event whose live read succeeds completes the sample and switches
   to FDT-up detection. A live read returning `-1` leaves the request and
   callback pending, rearms FDT-down, and returns the worker to its event wait.
5. An up IRQ runs the lift comparison, may attempt full-base acquisition, and
   then rearms FDT-down detection.
6. A reverse IRQ runs its comparison/acquisition path and rearms FDT-down
   detection.

The profile has an asynchronous hardware-event loop but no periodic software
polling. Environmental drift can be noticed while an operation waits because
the sensor remains armed against its programmed FDT base. The software only
evaluates the 12-area thresholds after the sensor emits an FDT event.

Reverse IRQs are part of the refresh contract rather than incidental noise. The
parser updates the hardware's down-arm base on each reverse event, while
`Reverse_Occure` maintains a separate normalized software anchor so multiple
smaller shifts can accumulate into a refresh decision. `UP_Occure` uses that
same anchor. See `usbinterface-FUN_180014480.md` and
`usbinterface-FUN_1800149c4.md`.

## Timer Distinction

Profile-9 `FUN_180014270` (`OnTimerFunc`) is not the environmental-refresh
mechanism. It clears a cached live frame and may rearm FDT-down detection in the
power-button timing path. It does not compare FDT areas or call
`MilanHV_temperature_event`.
