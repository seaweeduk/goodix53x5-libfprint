# usbinterface.dll FUN_180022d70

## Identity

- Address: `0x180022d70`
- Logged name: `usbEvtDeviceD0Entry`
- Role: UMDF device-power entry callback.

## Findings

- Clears the HAL/device stop byte at device-context nested offset `+0x68e0`.
- Resets the initialization event when initialization is not already complete.
- Restarts the continuous USB read pipe.
- Launches `FUN_180020970` (`deviceInit`) when the global init-thread handle is
  `-1`.
- It neither clears HAL image-base validity bytes `+0x232/+0x237` nor directly
  invokes `MilanHV_update_allbase`.
- A first D0 entry reaches full initialization and action `0x0c`; an ordinary
  resume reaches `deviceInit`'s resume branch and preserves the base.

## Stored Power State

`usbEvtDeviceD0Exit` (`FUN_180022ff0`) records the framework-reported system
power state in context dword `+0x168`. Before restarting the pipe or launching
the worker, D0 entry tests global byte `0x1800e2120`: when it is not exactly
one and signed `+0x168 > 1`, it writes `+0x168 = 0` and global byte
`0x18005f398 = 1`. Otherwise it preserves `+0x168`.

`DriverEntry` (`FUN_18002409c`) initializes that global from
`FUN_1800176e0` (`GetPowerInformation`). The helper queries power-information
level 4 and returns output byte `+0x14`, logged as Modern Standby support;
it returns zero when the query fails. This is a host capability value, not a
sensor-configuration or resource-continuity indicator.

Consequently, an initialized worker's handshake predicate is the normalized
state. With global `0x1800e2120 != 1`, a previously recorded state greater
than one does not itself cause a resume handshake. With that global equal
to one, the recorded state remains available to the worker's signed
greater-than-one test. This path contains no sensor-configuration readback
or base validation. See `usbinterface-FUN_180020970.md` for worker completion
and failure behavior.

## Lifetime Consequence

Selective-suspend and system-power D0 transitions are not equivalent to HAL
detach. The retained `+0x248` image base and its validity flags survive this
callback pair as long as WDF does not also schedule `ReleaseHardware`.
