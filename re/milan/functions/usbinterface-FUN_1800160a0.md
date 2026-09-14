# usbinterface.dll FUN_1800160a0

## Identity

- Address: `0x1800160a0`
- Logged name: `milan_HVseries_disable`
- Role: final HAL-context teardown for Milan HV profile 9.

Profile-0 `FUN_180011160` is a separate close callback. Unlike this owner,
it has no frees of retained image/auxiliary/FDT buffers at
`+0x248/+0x258/+0x268`. Both are reached after the common controller-stop
helper. See [Profile 0 USB Contract](../PROFILE0-USB-CONTRACT.md) for the
explicit ownership boundaries; close behavior must not be inferred solely
from the shared HAL layout.

## Call Path

- `usbinterfaceEvtDeviceReleaseHardware` (`FUN_180023c40`) calls
  `device_disable` (`FUN_18000e8cc`).
- `deviceInit` (`FUN_180020970`) also calls `device_disable` at
  `0x180020b8a` after `device_enable` failure or a nonzero action-9 sensor-check
  result. When profile 9 was enabled, this reaches the same close callback
  before publishing device-context initialized byte `+0x110`.
- `device_disable` invokes the HAL close callback at its context `+0x1c8`;
  profile-9 initialization installs this function there.
- D0 exit/entry, request cancellation/reset, queue stop, operation
  deactivation, and display-power notification do not call `device_disable`.

## Findings

- Clears the HAL-enabled global byte and calls `FUN_18000445c`
  (`GxFNHV_MilanClose`).
- Stops timers, closes event handles, and deletes the HAL critical section.
- Frees retained image-base buffer `DAT_180061838`, HAL context offset `+0x248`,
  and sets its pointer to null.
- Also frees the auxiliary buffer at `+0x258` and retained FDT-calibration
  storage at `+0x268`.
- It does not visibly clear image-base validity bytes `+0x232/+0x237` or the
  one-shot refresh byte `+0x236`.

## Lifetime Consequence

This is the hardware-context lifetime boundary. The physical image-base storage
does not survive it. A later full `deviceInit` allocates a new buffer and issues
action `0x0c`; profile initializer `FUN_1800162ac` clears the base/image validity
bytes before allocating that replacement buffer.
