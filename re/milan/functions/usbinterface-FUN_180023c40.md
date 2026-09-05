# usbinterface.dll FUN_180023c40

## Identity

- Address: `0x180023c40`
- Logged name: `usbinterfaceEvtDeviceReleaseHardware`
- Role: full UMDF hardware-release callback.

## Findings

- Marks the nested HAL/device context stopped and waits for any outstanding
  `deviceInit` worker.
- Calls `device_disable` (`FUN_18000e8cc`) at `0x180023e27`.
- `device_disable` stops the HAL event thread and invokes the profile close
  callback; profile 9 reaches `FUN_1800160a0` (`milan_HVseries_disable`).
- Destroys driver synchronization state, GTLS state, notification registration,
  and device initialization handles after HAL teardown.

## Lifetime Consequence

Unlike D0 exit, this callback ends the hardware-context image-base lifetime.
The retained `+0x248` buffer is freed below `milan_HVseries_disable`.
`FUN_1800208c4` destroys the port GTLS state and its synchronization objects.
This callback does not clear the device-context initialized byte `+0x110`;
device-object initialization in `FUN_1800232a8` owns its explicit zero store.
A new device context consequently enters full `deviceInit`, but the release
callback alone does not establish that a subsequent entry using an old context
will take that branch.

The callback is registered by `FUN_1800232a8` as a framework hardware-release
callback, not as a client file-close callback. `device_disable` has direct
callers here and in `deviceInit` initialization-failure handling. D0 exit and
queue `usbinterfaceEvtIoStop` (`FUN_1800243c0`) do not call it. The latter
cancels or acknowledges a stopped request and clears pending request `+0xf8`,
without freeing the HAL image base or clearing `+0x110`.
