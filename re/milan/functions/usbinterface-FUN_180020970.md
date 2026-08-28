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
- `device_enable` calls `thunk_FUN_18001b6c8(0, NULL)` at `0x18000ea34..0x18000ea38`
  before chip-ID/profile selection. `FUN_18001b6c8` (`Reset`) sends the
  sensor-reset request through category `0x0a`, command `1`, with payload bytes
  `01 14`; no USB bus reset is issued by this path.
- After sensor check and GTLS handshake, it issues action `0x0a` at
  `0x180020cd9`, then action `0x0c` at `0x180020d04`.
- `FUN_18000e1f0` action `0x0c` invokes HAL slot `+0x180`; for profile 9 this
  is `FUN_180015c60` (`MilanHV_update_allbase`).
- The worker does not inspect the action-`0x0c` return before continuing.
  An image-pair or final TX-on FDT rejection whose common postlude succeeds
  returns zero in any case, so initialization continues without a valid image
  reference or an externally reported initialization error.

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

## Initialization Predicate Boundary

This function consumes device-context byte `+0x110` as the full-initialization
versus resume predicate. The producer that sets the byte is outside this
function.
