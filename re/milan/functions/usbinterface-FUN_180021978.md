# usbinterface.dll FUN_180021978

## Identity

- Address: `0x180021978`
- Logged name: `OnCaptureData`
- Role: UMDF biometric capture-request handler shared by enrollment and
  identify/verify purposes.
- Standard IOCTL dispatch: `FUN_180024220:0x180024258..0x180024288` maps
  `0x440014` directly to this function.
- Current mapping: `goodix_scan_start_coordinator_subsm()` and the live capture
  submachine in `drivers/goodix53x5/device/scan.c`; `auth.c` and `enroll.c` own
  the resulting libfprint operation completion.

## Findings

- Retains at most one WDF request at device context `+0xf8`. A second request
  is completed immediately with `0xc0000476`. The retained request installs
  cancel callback `FUN_180020ef0` (`gfOnCancel`).
- Requires at least `0x20` input bytes and four output bytes. If the output is
  smaller than `0x1d890`, it writes required size `0x1d890` at output `+0` and
  completes successfully with information length four.
- Validates accepted WBF purpose byte `+0x04` values `1`, `2`, `4`, `8`, `16`,
  and `0x80`, format owner/type words `+0x06 == 0x1b` and `+0x08 == 0x0401`,
  and header size byte `+0x1c == 0x20`.
- Enrollment purpose `4` calls `FUN_18000ebcc` with capture selector `2`;
  other accepted biometric purposes call it with selector `1`.
- Both routes register the same `CaptureFramedone` callback
  (`FUN_18001fb40`) and use the same HAL context.
- The call to `FUN_18001f4c0` at `0x180021e47` is a no-op: its entire body
  is `RET 0`. It leaves the caller-initialized completion byte at zero, so
  the valid-request route continues through the initialization-event wait at
  `0x180021ec3` to `FUN_18000ebcc` at `0x180021ee1`. It performs no cached
  completion or sensor acquisition.
- The request is retained at device-context `+0xf8` before `FUN_18000ebcc` is
  called. In `FUN_18000ebcc`'s enabled branch (global byte `0x18005f398`
  nonzero), first-activation byte `+0x154` selects
  FDT-down; otherwise HAL wait state `+0x1fc == 0xf1` selects FDT-up and every
  other value selects FDT-down. The selected arm callback completes before
  `FUN_18000ebcc` stores `CaptureFramedone` at HAL context `+0x240`.
- Global byte `0x18005f398` has image-initialized value one. A fresh enabled
  request therefore takes this arm-selection branch without requiring a
  preceding sensor IRQ or successful image-base admission.
- Consequently, the first ordinary request after an initial acquisition that
  returned with no valid image reference arms FDT-down. Neither `OnCaptureData`
  nor `FUN_18000ebcc` derives an FDT-up base from the rejected acquisition's
  final manual packet.
- The function does not clear image-base flags and does not call
  `MilanHV_update_allbase`.
- `FUN_18000ebcc` changes finger-detection/capture state and installs the live
  callback, but likewise does not acquire a new base. FDT down/up handlers may
  independently invoke the temperature-refresh path if their base comparison
  detects drift.
- The request-start power-control call through `FUN_180017ef0` reaches
  `FUN_18001afec`, which sends category `0x0a`, command 7. Its parameters are
  the two power-control bytes plus a zero byte; it is not category-3 manual
  FDT acquisition. Profile arm callback `+0xb0` sends down/up detection,
  not manual detection. Neither call supplies replacement validation data
  after an initial acquisition rejection.

## Pending And Completion Ownership

- A valid request with sufficient output remains pending after this function
  returns. Device context `+0x100` retains its output-buffer pointer while
  `+0xf8` retains the request.
- `CaptureFramedone` completes the retained request only after a live frame has
  been delivered. It writes total size `0x1d890` at output `+0`, status zero at
  `+0x04`, result one at `+0x08`, zero at `+0x0c`, payload size `0x1d878` at
  `+0x10`, and the sample payload from `+0x14`. Completion status is zero and
  information length is `0x1d890`; completion clears request owner `+0xf8`.
- A live-image callback failure does not call `CaptureFramedone`, write the
  output, or complete the request. The request, output pointer, and registered
  HAL callback remain owned for a later FDT-down attempt.
- When initial image validity is clear, the first down and following up handlers
  likewise leave the request at `+0xf8` pending. If the up path admits a fresh
  reference, the next down completes that same request. Given identical retained
  and live frames, its `0x1d890`-byte WBF output equals an ordinary successful
  capture except for the one-shot refresh-marker byte.
- `gfOnCancel` sets device-context stop bytes `+0x154/+0x152`, completes the
  retained request with `0xc0000120`, and clears request owner `+0xf8`. It does
  not synthesize a completed-frame payload.

## Lifetime Consequence

Enrollment and identify/verify are separate biometric operations above one
retained hardware base. Ending one request does not end the base lifetime. The
ordinary live owner frees its temporary frame and clears the callback only
after successful callback dispatch; a failed live read leaves the callback and
request pending.
