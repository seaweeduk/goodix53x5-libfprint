# usbinterface.dll FUN_180023c40

## Identity

- Address: `0x180023c40`
- Logged name: `usbinterfaceEvtDeviceReleaseHardware`
- Role: full UMDF hardware-release callback.

## Findings

- Marks the nested HAL/device context stopped. It waits for `deviceInit` only
  when its global handle `0x1800600a8` is not `-1` and byte `0x1800600a4`
  equals one. The first wait is 3000 ms; timeout (`0x102`) causes a second
  6000-ms wait. Handle close and restoration of the `-1` sentinel occur inside
  this conditional branch. Reader failure clears the byte and therefore skips
  this wait branch; profile disable and the following teardown remain outside
  it. See [reader failure and initialization gates](usbinterface-profile9-fdt-event-loop.md).
- Calls `device_disable` (`FUN_18000e8cc`) at `0x180023e27`.
- `device_disable` stops the HAL event thread and invokes the profile close
  callback; profile 9 reaches `FUN_1800160a0` (`milan_HVseries_disable`).
- Neither `device_disable` nor the profile-9 close callback requests a sensor
  mode or calls `EcControl`; hardware release adds no power command after HAL
  teardown.
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

`usbinterfaceEvtDeviceQueryRemove` (`FUN_180023b50`) and
`usbinterfaceEvtDeviceSurpriseRemoval` (`FUN_180023f00`) set the nested stop
byte and unregister the display-power notification through `FUN_18001782c`.
They do not disable the HAL, free `+0x248`, clear either validity byte, or clear
`+0x110`; actual destruction remains owned by a subsequent `ReleaseHardware`
callback. `usbinterfaceEvtQueryStop` (`FUN_180023ff0`) returns zero without any
of those mutations.

Device creation `FUN_1800232a8` registers PnP/power callbacks and the I/O queue,
but no file-object cleanup or close callback. The driver-context cleanup callback
`FUN_1800241d0` releases its separate global container through `FUN_18002d0a0`
and does not call `device_disable`. Client-file closure therefore has no direct
image-base mutation in this DLL; framework scheduling of request stop,
deactivation, device release, and driver cleanup is a separate boundary.
