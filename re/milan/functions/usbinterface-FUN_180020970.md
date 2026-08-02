# usbinterface.dll FUN_180020970

## Identity

- Address: `0x180020970`
- Logged name: `deviceInit`
- Role: asynchronous initialization worker launched from UMDF D0 entry.

## Initial Image-Base Path

- On the first/full initialization branch (`device_context +0x110 != 1`), it
  calls `FUN_18000e9b0` (`device_enable`). Profile 9 reaches
  `FUN_1800162ac`, which allocates the HAL buffers and installs
  `MilanHV_update_allbase` at HAL slot `+0x180`.
- After sensor check and GTLS handshake, it issues action `0x0a` at
  `0x180020cd9`, then action `0x0c` at `0x180020d04`.
- `FUN_18000e1f0` action `0x0c` invokes HAL slot `+0x180`; for profile 9 this
  is `FUN_180015c60` (`MilanHV_update_allbase`).
- The worker does not inspect the action-`0x0c` return before continuing.

## Resume Branch

- When device-context byte `+0x110 == 1`, the worker does not call
  `device_enable`, action `0x0a`, or action `0x0c`.
- For a prior power state greater than one it retries the GTLS handshake, but
  there is no image-base invalidation or refresh in this branch.
- Therefore a D0 resume retains the existing HAL-context base unless some
  independent FDT/temperature path invalidates it.

## Scheduling

- `FUN_180022d70` (`usbEvtDeviceD0Entry`) creates this worker whenever the
  global init-thread handle is the sentinel `-1`.
- The distinction between first initialization and resume is the persistent
  device-context initialized byte, not the biometric operation type.

## Unresolved

- The exact WDF event that sets device-context `+0x110` to one is outside this
  function; observed control flow treats it as the completed-initialization
  predicate.
- Whether failed action `0x0c` is surfaced by a later framework status path.
