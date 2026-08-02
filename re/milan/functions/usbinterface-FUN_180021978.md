# usbinterface.dll FUN_180021978

## Identity

- Address: `0x180021978`
- Logged name: `OnCaptureData`
- Role: UMDF biometric capture-request handler shared by enrollment and
  identify/verify purposes.

## Findings

- Validates the WBF capture request and selects operation behavior from request
  purpose byte `+0x04`.
- Enrollment purpose `4` calls `FUN_18000ebcc` with capture selector `2`;
  other accepted biometric purposes call it with selector `1`.
- Both routes register the same `CaptureFramedone` callback
  (`FUN_18001fb40`) and use the same HAL context.
- The function does not clear image-base flags and does not call
  `MilanHV_update_allbase`.
- `FUN_18000ebcc` changes finger-detection/capture state and installs the live
  callback, but likewise does not acquire a new base. FDT down/up handlers may
  independently invoke the temperature-refresh path if their base comparison
  detects drift.

## Lifetime Consequence

Enrollment and identify/verify are separate biometric operations above one
retained hardware base. Ending one request does not end the base lifetime; the
completed-frame dispatcher frees only the live frame and callback.
