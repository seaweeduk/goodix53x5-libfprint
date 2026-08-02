# usbinterface.dll FUN_1800160a0

## Identity

- Address: `0x1800160a0`
- Logged name: `milan_HVseries_disable`
- Role: final HAL-context teardown for Milan HV profile 9.

## Call Path

- `usbinterfaceEvtDeviceReleaseHardware` (`FUN_180023c40`) calls
  `device_disable` (`FUN_18000e8cc`).
- `device_disable` invokes the HAL close callback at its context `+0x1c8`;
  profile-9 initialization installs this function there.

## Findings

- Clears the HAL-enabled global byte and calls `FUN_18000445c`
  (`GxFNHV_MilanClose`).
- Stops timers, closes event handles, and deletes the HAL critical section.
- Frees retained image-base buffer `DAT_180061838`, HAL context offset `+0x248`,
  and sets its pointer to null.
- Also frees the nav/live auxiliary buffers at `+0x258/+0x268` equivalents.
- It does not visibly clear image-base validity bytes `+0x232/+0x237` or the
  one-shot refresh byte `+0x236`.

## Lifetime Consequence

This is the hardware-context lifetime boundary. The physical image-base storage
does not survive it. A later full `deviceInit` allocates a new buffer and issues
action `0x0c`, but static flags can remain stale until that acquisition
succeeds unless DLL unload/zeroing occurs externally.
