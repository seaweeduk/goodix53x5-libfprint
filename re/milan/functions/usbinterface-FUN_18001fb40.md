# usbinterface.dll FUN_18001fb40

## Identity And Inputs

- Address: `0x18001fb40`; logged name: `CaptureFramedone`.
- Standard profile-9 capture calls this routine synchronously from
  `MilanHV_ReadImg` (`0x1800150e0`), before the live buffer is freed and before
  `MilanHV_Down_procedure` invokes the sensor-mode callback and arms FDT-up. See
  `usbinterface-FUN_180014e10.md`.
- Current counterpart: the capture-ready boundary in
  `drivers/goodix53x5/device/scan.c`; `goodix_auth_capture_ready` in `auth.c`
  submits the retained reference and live frame to the runtime worker.
- Effective callback arguments are device handle, retained image base, live
  frame, live-frame byte length, and a one-byte base-refresh marker.
- Device-context lookup uses WDF table slot `+0x3d8`. A null handle, context,
  context `+0x160`, or HAL pointer at `[context+0x160]+8` returns without
  publication. The ordinary active request supplies all of these owners.

## Sample Construction

- `CaptureFramedone` copies calibration words `+0x31e`, `+0x312`, and `+0x310`
  directly into the sample trailer as `tcode_use`, `dac_h`, and `dac_l`.
- There is no per-capture arithmetic at this boundary.
- At `0x18001fcbc..0x18001fcd9`, the second argument is copied as one
  `rows * columns * 2` frame into staging address `0x180093508`. In the engine's
  image-payload view this is offset `+0xebf0`, the frame passed to
  `preprocessor_init`.
- At `0x18001fca0..0x18001fcb7`, the third argument is copied to payload
  offset `+0x03`. The fifth argument is stored at payload offset `+0x00` and is
  the one-shot marker consumed by `GoodixEngineAdapter.dll` `FUN_18001f610`.
- This routine forwards a previously retained hardware-context frame; it does
  not capture, average, or transform a new reference.
- Payload `+0x01` receives HAL byte `+0x348`. Payload `+0x02` receives HAL byte
  `+0x30d` when configuration byte `+0x424` is exactly one, otherwise one.
- HAL timestamp dwords `+0x390/+0x394` are copied to
  `0x1800a20f8/0x1800a20fc`. Calibration words follow at
  `0x1800a2100/0x1800a2102/0x1800a2104`. These fields are copied, not adjusted.
- The reference length is the low 16 bits of device-context byte
  `+0x60 * +0x61 * 2`, or `0x4a40` for profile 9. The live copy uses the supplied
  byte length, also `0x4a40` for one-frame identify/verify.
- Only the specified live/reference spans and metadata are rewritten here;
  the routine does not clear the remainder of the fixed-size staging sample.

## Publication And Ownership

- With both request `context+0xf8` and output pointer `context+0x100` nonnull,
  writes output dwords `(size, status, result, reserved, payload_size)` as
  `(0x1d890, 0, 1, 0, 0x1d878)` at offsets `0, 4, 8, 0xc, 0x10`, then copies
  `0x1d878` bytes from staging `0x180084890` to output `+0x14`.
- Calls `CompletePendingRequest` (`0x18001ff08`) with status zero and information
  length `0x1d890`. The helper calls WDF unmark-cancelable slot `+0x4e0`; unless
  it returns `0xc0000120`, it calls completion slot `+0x520` and then clears
  request `+0xf8`. It leaves output pointer `+0x100` unchanged. Cancellation
  retains its separate completion owner.
- Device-context flag dword `+0xf0 == 1` brackets sample construction and
  completion with critical section `+0xc8` (the marker write precedes entry).
  The completion helper separately uses critical section `+0x98` when dword
  `+0xc0 == 1`.
- This function neither clears the registered HAL callback nor frees the live
  buffer or retained reference. `MilanHV_ReadImg` clears the marker/callback and
  frees only its temporary live buffer after this callback returns. A read
  failure never enters this routine, so neither staging nor request output is
  published by that attempt.
- Request admission and lifetime are owned by `OnCaptureData`; see
  `usbinterface-FUN_180021978.md`.

## Cancellation And Later Arm Failure

`gfOnCancel` (`0x180020ef0`) takes the same construction critical section
`+0xc8` when enabled, sets stop bytes `+0x154/+0x152`, and takes request
critical section `+0x98` when enabled. With a retained request it completes
the callback's request argument through WDF slot `+0x518` with status
`0xc0000120`, then clears `+0xf8`. It does not free either frame, clear the HAL
capture callback, or destroy the retained base.

If cancellation owns the request before sample construction, the null request
gate prevents output publication. If unmark-cancelable reports `0xc0000120`
after the sample bytes were copied, `CompletePendingRequest` leaves completion
to `gfOnCancel`: copied bytes do not constitute successful request completion.
If normal unmark/completion wins, the success status and sample length have
already been delivered and `+0xf8` is clear before the down handler arms up.
That later arm has no request owner to complete again or change to failure.
Its transport result is not propagated by the profile-9 arm wrapper; see
`usbinterface-FUN_180014e10.md`.
