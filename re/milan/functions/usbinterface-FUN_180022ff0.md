# usbinterface.dll FUN_180022ff0

## Identity

- Address: `0x180022ff0`
- Logged name: `usbEvtDeviceD0Exit`
- Role: UMDF device-power exit callback.

## Findings

- Records the current system power state in device context `+0x168`.
- Sends device action `0x13` and, for deeper states, action `0x0e` to put the
  sensor to sleep.
- Sets the nested HAL stop byte at `+0x68e0` and stops the continuous USB read
  pipe.
- It does not call `device_disable`, `milan_HVseries_disable`, or
  `MilanHV_update_allbase`.
- It does not clear HAL validity bytes `+0x232/+0x237`, one-shot refresh byte
  `+0x236`, or free retained base buffer `+0x248`.

## Lifetime Consequence

The normal D0 exit/entry pair preserves hardware image-base state. Whether a
particular suspend/hibernate/removal also causes `ReleaseHardware` is decided by
UMDF/Windows scheduling outside this callback.
