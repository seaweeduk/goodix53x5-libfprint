# usbinterface.dll FUN_1800174a0

## Identity

- Address: `0x1800174a0`
- Logged name: `PowerNotificationCallback`
- Role: display-power notification handler.

`DriverEntry` calls registration owner `0x1800177c8` after successful WDF
driver creation. That owner unregisters an existing handle, then calls
`PowerSettingRegisterNotification` for GUID storage `0x180059dc0`, flags 2,
and callback `0x1800174a0` with null context. The notification subscription is
driver-owned, not installed by a capture request.

## Power-Capability Selector

`DriverEntry` (`0x18002409c`) calls `GetPowerInformation` (`0x1800176e0`)
and stores its byte return at `0x1800e2120`. The getter zeroes a 76-byte
buffer, calls `CallNtPowerInformation` with information level 4, and returns
buffer byte `+0x14`; a negative status forces zero. Its diagnostic names this
byte "supports Modern Standby". This is a retained capability result, not the
current display state or a pending fingerprint request.

## Display-Event Branches

The callback accepts its selected power-setting GUID and treats a zero data
dword as screen off, every nonzero value as screen on. Its local arm argument
starts at one and its six-byte EC-control record starts at zero.

- **Screen off:** clear screen-active byte `0x18005f398`. If capability byte
  `0x1800e2120 != 1`, return without a device action. Otherwise issue action
  `0x17` (clear HAL power-button byte `+0x358`), action `0x11` with initial
  control `00 00`, delay zero and timeout 200, then action `7` with byte one
  to arm FDT-down. The final arm occurs at `0x180017566..0x18001756f`.
- **Screen on:** set `0x18005f398 = 1`. When global handshake event
  `0x180084860` is nonnull, wait at most 3000 ms and ignore the wait result.
  If `0x1800e2120 != 1`, dispatch only action `0x16` and return.
- **Screen on, capability exactly one:** dispatch action `0x15` to deliver
  any eligible retained frame, then action `0x11` with initial control
  `01 00`, delay zero and timeout 200. Read configuration byte `+0x442`:
  zero selects action `7` argument one (FDT-down); nonzero selects argument
  zero (FDT-up). The selector is at `0x180017670..0x1800176ba`.

The callback does not gate these branches on capture-request ownership or
inspect action return values. Action `7` itself requires an enabled HAL and
nonnull argument/callback, but has no sleep-mode or pending-capture gate.
Action `0x16` instead acts only when HAL mode `+0x1e0 == 0`; after deactivation
has set mode 2, this alternate screen-on route performs no EC/arm operation.
The exact dispatcher consumers are owned by
[the action dispatcher](usbinterface-FUN_18000e1f0.md).

None of these branches directly invokes `MilanHV_update_allbase`, clears
`+0x232/+0x237`, or frees `+0x248`. Subsequent received FDT events have their
own refresh/health handlers; see
[the event loop](usbinterface-profile9-fdt-event-loop.md).

## Lifetime Consequence

Display off/on and modern-standby screen notifications preserve the retained
hardware image base in the static DLL graph. A separate WDF D0 or hardware
release callback may still be scheduled by Windows.

## Current Source Map

There is no display-notification counterpart in the current Linux driver.
`drivers/goodix53x5/device/scan.c` owns action-scoped EC/arm operations;
`device/session.c` owns suspend/reinitialization. Those owners do not implement
this capability-selected display callback or its request-independent arms.
