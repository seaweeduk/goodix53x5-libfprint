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
The retained `+0x248` buffer is freed below `milan_HVseries_disable`. A later
attach must run full `deviceInit` to allocate and acquire a replacement.
