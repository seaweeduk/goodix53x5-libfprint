# usbinterface.dll FUN_1800174a0

## Identity

- Address: `0x1800174a0`
- Logged name: `PowerNotificationCallback`
- Role: display-power notification handler.

## Findings

- Screen off clears a screen-active global and issues actions `0x17` and
  `0x11` for power isolation/state handling.
- Screen on waits for GTLS readiness, issues actions `0x15` and `0x11`, then
  restores finger down/up handling through action `7` or `0x16`.
- None of these actions dispatches HAL callback slot `+0x180`.
- It does not clear `+0x232/+0x237`, free `+0x248`, or invoke
  `MilanHV_update_allbase`.

## Lifetime Consequence

Display off/on and modern-standby screen notifications preserve the retained
hardware image base in the static DLL graph. A separate WDF D0 or hardware
release callback may still be scheduled by Windows.
