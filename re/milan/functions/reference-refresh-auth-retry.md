# Profile-9 Reference Refresh Across Authentication Attempts

## Scope

This note covers only sensor type 12, which selects profile 9. It compares the
native Windows path in `usbinterface.dll` and `GoodixEngineAdapter.dll` with the
current Linux driver for a lock screen left waiting for hours. Elapsed time is
not itself a native refresh trigger.

## Shared Baseline

Both implementations acquire a validated low-DAC TX-on/TX-off pair during
initial hardware setup and retain the TX-on image as preprocessing reference.
Normal biometric operation boundaries reuse that reference:

- Windows capture requests do not call `MilanHV_update_allbase`.
- `EngineAdapterClearContext` clears per-operation state but retains initialized
  preprocessing calibration.
- Linux auth reference ensure returns immediately while
  `milan_generation` exists.

## Client Lifetime

Hyprlock commit `b222d9b1` starts fingerprint authentication during
`CAuth::start`, before it acquires the session lock
(`src/auth/Auth.cpp:22-25`, `src/core/hyprlock.cpp:355-379`). When the machine is
not entering sleep, `CFingerprint::init` immediately calls `startVerify`; no
keyboard input or finger event starts the operation
(`src/auth/Fingerprint.cpp:50-81`). The device claim is retained until
termination (`Fingerprint.cpp:99-102`, `:258-271`), and there is no first-probe
timeout in this path. A Hyprlock action can therefore wait with the Linux
open-time reference for the full lock duration.

On terminal no-match Hyprlock calls `VerifyStop` and schedules another
`VerifyStart` while retaining the claim (`Fingerprint.cpp:141-163`, `:216-255`).
That produces a new libfprint driver action over the same open device and Milan
generation. On match it stops verification and unlocks (`Fingerprint.cpp:168-172`).

This lifetime is client-specific. The reference-refresh divergence applies to
any greeter that keeps the device open across the wait or across retries, but
the multi-hour first-action premise must not be assumed for PAM-based or other
greeters without tracing that client's authentication timing.

## First Probe After A Long Wait

Windows does not poll reference quality on a timer. A controller thread sleeps
on a profile event while the sensor is armed for FDT-down detection. The
profile-9 MCU parser wakes it only when an FDT IRQ arrives; the selected handler
then performs the comparisons and any refresh synchronously. See
`usbinterface-profile9-fdt-event-loop.md`.

Native `MilanHV_Down_procedure` compares the FDT interrupt sample with an
immediate manual TX-off reading. If every area remains within threshold, native
classifies the event as drift/noise, synchronously refreshes all bases, and
rearms without taking a live image. The eventual real probe then carries the
new TX-on reference and one-shot preprocessing-refresh marker.

Linux performs the same event/manual comparison and rearms a false event, but
does not refresh its FDT/image-base generation. Therefore:

- No native false-event refresh: both first probes use the open-time reference.
- Native false-event refresh: Windows uses a fresh reference; Linux does not.
- First-probe success is terminal on both sides; there is no same-lock retry to
  analyze after successful unlock.

## Failed First Probe And Second Probe

Windows finger-up handling compares the 12-area lift reading with its retained
software drift anchor. Reverse events seed and maintain this anchor separately
from the base programmed into the sensor. If an anchor is active and more than
half the areas exceed the configured down threshold, finger-up runs the same
full-base refresh and marks the next completed sample. A normal WBF clear-context
boundary does not undo that refresh or independently reset preprocessing.

Linux reports no-match/retry, waits for lift, derives a new FDT-down base from
the lift event, and completes the libfprint action. Hyprlock/fprintd then starts
a new driver action, but the existing Milan generation causes reference ensure
to skip capture. Therefore:

- Native lift threshold does not fire: both second probes reuse the open-time
  reference.
- Native lift threshold fires: Windows refreshes before the second probe;
  Linux retains the open-time reference.

Linux also does not currently preserve native reverse-IRQ classification or the
separate cumulative drift anchor. Matching only the immediate down-event
comparison is therefore insufficient for like-for-like behavior.

Native switches the sensor to FDT-up detection immediately after a real down
event and live capture, before the engine later reports match or no-match. Linux
currently chooses its up path only after the runtime result: no-match/retry waits
for lift, while success deactivates without running the up handler. Whether a
native up event wins the race with successful-operation teardown is not yet
proven, but the arm-before-result ordering is proven and should be preserved by
a like-for-like event loop.

## Validation Boundary

The static refresh and reuse contracts are proven from Ghidra and Linux source.
The hypothesis that hours of environmental change make the old reference harm
first-probe matching, and the frequency with which native thresholds fire,
remain unvalidated.

`GoodixEngineAdapter.dll` replay can compare preprocessing and matching under
chosen old/new setup frames without booting Windows. It cannot by itself execute
the physical FDT event logic, which resides in `usbinterface.dll`. Validation
should therefore first use existing Linux diagnostic frames and controlled DLL
replay or a focused userspace oracle for the documented profile-9 comparison
functions. USB traffic capture and dual boot are not prerequisites for this
static or DLL-level parity work.

## Driver-Team Assessment

Verdict: the current Linux implementation has a conditional profile-9 parity
gap. The condition is native FDT drift detection, not elapsed time and not every
authentication retry. Reproducing a multi-hour failure is not required to
establish this source-level difference.

The native parity target is constrained by the recovered behavior:

- On the false FDT-down comparison, refresh the complete validated FDT/TX-on/
  TX-off base set before rearming; do not consume a live probe for that event.
- On the finger-up majority-area comparison, refresh the same complete base set
  before the next probe only when the native threshold fires.
- After a successful refresh, make the next sample initialize preprocessing from
  the new retained TX-on reference exactly once.
- Do not recapture merely because a new verify/identify operation starts or a
  no-match occurs; native operation clearing normally retains calibration.

Implementation work still needs to determine how these synchronous native
transitions map safely onto the Linux SSMs and cancellation model. That design
question does not weaken the parity finding and should not be answered by an
unconditional per-probe refresh, which would exceed native behavior.

## Authorities

- Native base acquisition: `usbinterface-FUN_180015c60.md`
- Native FDT scheduling: `usbinterface-profile9-fdt-event-loop.md`
- Native down-event refresh: `usbinterface-FUN_180014e10.md`
- Native lift refresh: `usbinterface-FUN_1800149c4.md`
- One-shot hardware handoff: `usbinterface-FUN_18000e1f0.md`
- Engine sample refresh decision: `FUN_18001f610.md`
- Engine operation clear: `FUN_18001f090.md`
- Engine identify result: `FUN_180020b30.md`
- Linux generation reuse: `drivers/goodix53x5/device/base.c:746-750`
- Linux no-match/lift path: `drivers/goodix53x5/device/auth.c:314-345` and
  `drivers/goodix53x5/device/scan.c:430-455`
