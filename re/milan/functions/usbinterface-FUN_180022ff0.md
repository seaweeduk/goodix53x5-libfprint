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
