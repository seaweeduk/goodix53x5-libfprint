# usbinterface.dll FUN_180020970

## Identity

- Address: `0x180020970`
- Logged name: `deviceInit`
- Role: asynchronous initialization worker launched from UMDF D0 entry.

This worker's reset/session ordering is shared by profile 0 and profile 9.
Family selection occurs inside `FUN_18000e9b0`; action 9 and action 12 then
dispatch different sensor-check and all-base callbacks. Both full-init and
resume session routes call the same `FUN_180007ee0` wrapper and common
GTLS client owners. See [Profile 0 USB Contract](../PROFILE0-USB-CONTRACT.md)
for the family boundary, profile-0 validity producer, and cancellation scope.

## Full-Initialization Firmware Probe

Before `device_enable`, the uninitialized branch runs a maximum of five probe
iterations. Each iteration calls `ChangeMode(0, 0, 0, NULL, 500)` through
`0x180017ec0`, ignores that result, and then calls version wrapper `0x180017fe8`
with destination device context `+0x111` and response budget 2000. The category-0
command is inside the loop, not a one-time prelude.

Version wrapper `0x180017fe8` rejects a null destination with `0xffffffff`;
otherwise it forwards to `GetEvkVesion` (`0x18001b15c`). The getter sends
category `0x0a`, command 4, payload `00 00`, checksum enabled, ACK budget 500,
response budget `(uint16_t) supplied_timeout`, and response selector 3
(event handle `0x1800688f0`). It makes a second identical transaction only
when the first sender result is zero. Each attempt resets response readiness
before sending and uses the ordinary odd-ACK predicate. Response waiting tests
event completion, not a version status byte or string predicate.

For this query `DataFromDevice` (`0x18001a7ec`) copies the received payload,
excluding its checksum, to shared cache `0x1800638d3` and signals selector 3.
The parser does not clear the remainder of that cache; the getter does not
clear it before sending. On successful sender completion the getter copies
exactly 64 cache bytes to its caller and returns zero. It neither returns the
received string length nor appends a terminator or checks a firmware prefix.
On two zero sender results it returns `0xffffffff` and leaves the caller's
64-byte destination untouched. A zero response timeout disables event waiting
in the generic sender; this is distinct from a response packet with no version
characters and is not the timeout used by `deviceInit`.

Selector 3 and its cache are not private to the version command.
`DataFromDevice` routes category-`0x0a` commands **0, 1 and 4** through the
same payload-copy and `SetEvent(0x1800688f0)` branch. There is no active-command
predicate. A response for command 0 or 1 can therefore satisfy a pending
version wait and supply that getter's 64-byte cache projection, including when
received before the version ACK. Category-`0x0a` command 3 also replaces the
cache prefix but signals selector 4 instead. Category 8 copies its payload to
the same cache and signals selector 0. These non-version writes affect retained
suffix bytes observable in a later short version response without themselves
signalling version readiness.

The same parser's category-`0x0e` branch writes a dword payload length at cache
offset zero and copies payload at offset four, signalling selector 6;
category-`0x0f` replaces only cache byte zero and signals selector 2. Those
stores also belong to the shared-cache lifetime. The version getter observes
only its first 64 bytes; it does not expose the full backing allocation or
require a separate version-only cache.

Only getter result `-1` increments the outer iteration count and invokes
`Sleep(100)`. The sleep precedes the count comparison, so the fifth failed
iteration also sleeps 100 ms. Success exits the loop immediately, without this
delay. Five failed iterations log version failure and then continue to the
existing initialization predicates and `device_enable`; exhaustion itself is
not a USB bus-reset command or an early thread return. The optional firmware
update check is entered only after a non-`-1` getter result when device-context
byte `+0x153 == 1`.

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

- The byte comparison at `0x180020a1b` selects resume only for exactly
  `device_context +0x110 == 1`. This branch skips the firmware-version loop,
  `device_enable`, sensor check action `9`, persisted-base action `0x0a`,
  refresh action `0x0c`, the final version query, and mode-2 action `0x0e`.
  It performs no sensor reset, profile discovery, configuration upload,
  power-mode restoration, FDT-store installation, or image-base validation.
- At `0x180020e62`, a signed dword comparison of context `+0x168` against two
  controls GTLS initialization. Values below two go directly to completion.
  This is the stored system-power state after D0-entry normalization, not an
  unmodified callback argument; see `usbinterface-FUN_180022d70.md`.
- Values at least two invoke `FUN_180007ee0` with the retained ring pointer
  at context `+0x190` and handshake-complete dword at `+0x198`. On nonzero
  return, the worker calls `FUN_18001b94c` only when the 32-bit absolute error
  is `0x700003`, then invokes `FUN_180007ee0` once more for any first error.
  The cache-clear helper zeros its two protocol cache buffers and counts;
  it is not a sensor reset or HAL/base teardown.
- `FUN_180007ee0` serializes on the GTLS critical section, requires a non-null
  ring and completion pointer, clears the completion dword and ring bytes
  `+0x0c..+0x13`, and initializes/handshakes the client. It sets completion to
  one only on success. Thus this route retains the resource allocation but
  reinitializes the session rather than merely validating an old session.
- Success, or the below-two route, signals global event `0x180084860`.
  A second nonzero handshake return exits without signalling that event.
  Neither route rewrites `+0x110`, signals context event `+0x108`, destroys
  resources, or falls back to full initialization. All exits restore the
  init-thread sentinel to `-1` and return thread result one; that result alone
  is not a handshake-success indication.
- HAL allocation, callback table, calibration, retained image/FDT buffers,
  and base-valid bytes remain as they were. In particular, resume does not
  establish that an already-clear image-valid byte became valid. Hardware
  configuration continuity is not measured by a readback here. A later
  operation-start callback separately requests mode 4, rebuilding and uploading
  configuration from retained calibration before arming FDT-down; that is not
  part of resume completion and does not refresh the bases. See the direct-mode
  contract in `usbinterface-FUN_18000e1f0.md`.

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
