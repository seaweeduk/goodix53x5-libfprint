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

## Linux Parity Status

The Linux suspend callback cancels an active transfer or state machine and
marks the sensor for reinitialization. It does not currently send the native
D0-exit sleep action or an EC power-isolation command before completing an
active-operation suspend.

Observed suspend/resume behavior is reliable on the profile-9/type-12 sensor,
including lock-screen use. Adding asynchronous sleep/EC cleanup is deferred
unless hardware evidence shows a suspend-entry or resume failure, because it
would need to sequence new USB transfers after cancelling an in-flight one.
