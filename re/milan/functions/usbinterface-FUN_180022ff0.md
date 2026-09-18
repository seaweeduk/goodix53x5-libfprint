# usbinterface.dll FUN_180022ff0

## Identity

- Address: `0x180022ff0`
- Logged name: `usbEvtDeviceD0Exit`
- Role: UMDF device-power exit callback.

## Findings

- Records the current system power state in device context `+0x168`.
- Only when Modern Standby capability byte `0x1800e2120 != 1`, sends action
  `0x13` with wait-FDT state `0xf2` and sets nested stop byte `+0x68e0 = 1`.
  Within that branch, context byte `+0x151 != 1` and signed power state
  `+0x168 >= 2` select action `0x0e` with mode 2 and timeout 200. Otherwise,
  exact context byte `+0x152 == 1` is cleared and action `0x11` is called with
  zeroed power-isolation fields and timeout 200. The action returns are not
  checked. With the capability byte equal to one, all these actions and the
  stop-byte write are skipped.
- Stops the continuous USB read pipe when its handle at context `+0x18` is
  non-null, independently of the capability byte. A recorded system-power
  state greater than one resets global completion event `0x180084860`.
- It does not call `device_disable`, `milan_HVseries_disable`, or
  `MilanHV_update_allbase`.
- It does not clear HAL validity bytes `+0x232/+0x237`, one-shot refresh byte
  `+0x236`, or free retained base buffer `+0x248`.

## Lifetime Consequence

The normal D0 exit/entry pair preserves host-side image-base state. Whether a
particular suspend/hibernate/removal also causes `ReleaseHardware` is decided by
UMDF/Windows scheduling outside this callback.
Neither callback measures sensor-configuration continuity or validates the
retained base. D0 entry can normalize the recorded power state before the
worker's handshake decision; see `usbinterface-FUN_180022d70.md` and
`usbinterface-FUN_180020970.md`. The mode callback's return and configuration
effects are documented in `usbinterface-FUN_18000e1f0.md`.

## Power-State And Command-Result Predicates

The callback's device-power target argument is not a reference-validity
predicate. The system-power value obtained through framework slot `+0x148`
is stored at `0x180023111`; its signed comparison at `0x180023168..0x18002316f`
selects sleep versus optional EC control. Neither branch changes image/FDT
validity, anchor or setup marker. Sleep dispatch at `0x18002318a` and EC
dispatch at `0x1800231df` have no return-status branch before reader stop;
failure does not invalidate the reference. The comparison at
`0x18002324b..0x180023257` clears handshake completion, not reference state.

On entry, `0x180022d70` saves the previous device-power argument in `EBP`
at `0x180022d8f` and uses it only in the diagnostic argument at
`0x180022f62`. Its reference/init route does not distinguish previous D1,
D2 or D3. The separate recorded system-power value is normalized at
`0x180022e68..0x180022e81` and subsequently controls only the initialized
worker's handshake choice. There is no sleep-generation, elapsed-sleep or
successful-power-down certificate consumed by the retained-reference path.

These predicates describe host state, not whether a particular device-power
transition physically removed sensor power. If hardware release also occurs,
its allocation teardown is separate; the saved-base file is not marked
optically invalid by this callback. See
[hardware release](usbinterface-FUN_180023c40.md) and the reset/configuration
composition in [deviceInit](usbinterface-FUN_180020970.md).
