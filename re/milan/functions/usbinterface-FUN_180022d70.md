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

## Lifetime Consequence

Selective-suspend and system-power D0 transitions are not equivalent to HAL
detach. The retained `+0x248` image base and its validity flags survive this
callback pair as long as WDF does not also schedule `ReleaseHardware`.
