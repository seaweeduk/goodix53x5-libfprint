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
  `01 14`; no USB bus reset is issued by this path. `device_enable` then calls
  `Sleep(10)` before chip-ID/profile discovery. This is a fixed post-reset
  delay, before the later base-acquisition action.
- After sensor check and GTLS handshake, it issues action `0x0a` at
  `0x180020cd9`, then action `0x0c` at `0x180020d04`.
- Action `0x0a` dispatches `FUN_18000d24c` to load persisted base data.
  If its file-read helper returns `-1`, it clears HAL byte `+0x231` at
  `0x18000d350`, frees the temporary buffer, and returns zero. This missing-file
  route does not publish image validity, acquire a sensor sample, or arm FDT;
  initialization proceeds to action `0x0c`.
- `FUN_18000e1f0` action `0x0c` invokes HAL slot `+0x180`; for profile 9 this
  is `FUN_180015c60` (`MilanHV_update_allbase`).
- A valid persisted-base load does not suppress action `0x0c` either.
  `FUN_18000d24c` can restore the image and FDT buffers, set persisted-base
  byte `+0x231`, and install FDT stores through slot `+0x68`. The worker still
  proceeds to action `0x0c` whenever the device remains active and is not
  stopping; it does not branch on the loader's return or `+0x231`.
- The worker does not inspect the action-`0x0c` return before continuing.
  An image-pair or final TX-on FDT rejection whose common postlude succeeds
  returns zero in any case, so initialization continues without a valid image
  reference or an externally reported initialization error.
- After action `0x0c`, the active, non-stopping route calls
  `thunk_FUN_18001b15c` at `0x180020d32` with the 64-byte version destination
  at device context `+0x111` and timeout 2000. This is a category-`0x0a`,
  command-4 version query, not a manual FDT read. The worker then dispatches
  action `0x0e` at `0x180020dad` with mode 2 and timeout 200. That dispatch
  invokes profile callback `+0x40`; profile 9 sends category-6 sleep mode.
  Neither continuation reacquires an FDT sample or checks image-valid state.

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
versus resume predicate. The full-initialization tail sets that byte to one at
`0x180020ded`, after the mode-2 action and before signalling completion events.
Image-base validation rejection does not bypass that publication.

## Initialized-Byte Lifetime

`FUN_1800232a8`, invoked by the framework device-add callback
`FUN_1800241c0`, writes device-context byte `+0x110 = 0` at
`0x180023467` during device-object initialization. The full `deviceInit`
tail writes one at `0x180020ded`. `usbEvtDeviceD0Entry` only tests the byte
at `0x180022e1f`; neither D0 callback clears it.

This is device-context state, not a per-capture or per-client flag. While the
same initialized context and HAL resources remain alive, another D0-entry
worker takes the resume branch rather than repeating action `0x0c`.
`usbinterfaceEvtDeviceReleaseHardware` does not clear `+0x110` either, but
destroys the HAL/base and GTLS resources. The byte alone therefore does not
establish resource validity after hardware release. See
`usbinterface-FUN_180023c40.md` for that distinct teardown boundary.

Request termination is separate from this lifetime. Queue dispatch
`FUN_180024220` routes request code `0x440008` to `FUN_18002296c`
(`OnReset`). With sufficient output capacity, that handler calls
`FUN_18001ff08` (`CompletePendingRequest`) with cancellation status and
returns an eight-byte successful reset response. It issues no sensor reset,
does not call `device_disable`, and does not clear `+0x110` or the base.
`CompletePendingRequest` only completes/unmarks the pending request and clears
its `+0xf8` owner when applicable.

The capture cancellation callback `FUN_180020ef0` (`gfOnCancel`) likewise
sets stop bytes `+0x154/+0x152` and completes/clears the pending request,
without clearing `+0x110`, freeing the retained base, or destroying GTLS.
These request callbacks are not the framework hardware-release callback;
their invocation alone does not cause another full `deviceInit`.
