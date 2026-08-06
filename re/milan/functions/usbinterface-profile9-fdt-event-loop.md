# usbinterface.dll Profile-9 FDT Event Loop

## Scope

This note covers sensor type 12, which selects profile 9. It documents the
scheduling and dispatch path around the already recovered
`MilanHV_Down_procedure`, `UP_Occure`, and `Reverse_Occure` threshold checks.

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

## Dispatch

`FUN_18000df20` maps the profile-9 event types through `FUN_18000e1f0`:

| Worker event | Device action | Profile callback |
| --- | --- | --- |
| `0x0f` | `0` | slot `+0x190`: `FUN_180014e10` (`MilanHV_Down_procedure`) |
| `0x10` | `1` | slot `+0x188`: `FUN_180015aa0` (`MilanHV_UP_procedure`) |
| `0x11` | `4` | slot `+0x198`: `FUN_180015a60` (reverse wrapper) |

`FUN_1800162ac` installs those three profile-9 callbacks. The up and reverse
wrappers call `UP_Occure` and `Reverse_Occure` respectively before rearming.

The worker executes the selected handler synchronously. If a handler calls
`MilanHV_temperature_event`, the complete `MilanHV_update_allbase` acquisition
and validation therefore finish on this worker before the handler rearms
detection and before the worker returns to its wait.

The down, up-wrapper, and reverse-wrapper paths rearm detection even when a
refresh attempt fails. Failed refresh leaves FDT base-valid byte `+0x232`
cleared and does not set one-shot byte `+0x236`; the prior image-base-valid byte
and retained image storage are not cleared before the attempt. Later event paths
can retry because `Milan_checkbase_isok` and invalid-base branches call
`MilanHV_update_allbase` again. This validity split is part of the native
failure contract and differs from treating one Linux generation as wholly valid
or absent.

## Hardware Arming And Rearming

`FUN_18000450c` installs `FUN_180005a60` at profile slot `+0xb0`:

- Argument `1` issues the profile-9 FDT-down detect command using the retained
  down-arm base and records wait state `0xf0`.
- Argument `0` issues the FDT-up detect command using the calculated up-arm base
  and records wait state `0xf1`.

The resulting state transitions are:

1. A capture request arms FDT-down detection and leaves the worker asleep.
2. A down IRQ wakes the worker and runs `MilanHV_Down_procedure`.
3. A false/drift down event synchronously refreshes all bases and rearms down;
   no live image is captured.
4. A real down event captures the live image and switches to FDT-up detection.
5. An up IRQ runs the lift comparison, optionally refreshes all bases, and then
   rearms FDT-down detection.
6. A reverse IRQ runs its comparison/refresh path and rearms FDT-down detection.

Thus native has an asynchronous hardware-event loop but no periodic software
polling. Environmental drift can be noticed while Windows Hello waits because
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

## Confidence

High from the profile-9 callback assignments, MCU IRQ parser, blocking worker,
device-action switch, handler bodies, and explicit down/up arm commands. No
behavior from the unrelated Milan F-series event path is used here.
