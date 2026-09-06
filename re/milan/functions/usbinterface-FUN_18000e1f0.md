# usbinterface.dll FUN_18000e1f0

## Identity

- Address: `0x18000e1f0`
- Role: hardware command/event dispatcher containing the completed-frame
  callback handoff.

## Reference Handoff

At `0x18000e5d9..0x18000e6b3`, the completed-image event requires both the
registered callback at context `+0x240` and live frame at `+0x340`. It invokes:

```c
callback(device_context,
         context->image_base_248,
         context->live_frame_340,
         context->live_frame_size_34c,
         context->base_refresh_236);
```

The call is at `0x18000e652`. It then clears refresh byte `+0x236`, frees the
live frame, and clears the callback. The retained image base is not freed or
modified.

This device-action-`0x15` handoff is an alternate completed-image event route.
On the ordinary standard profile-9 down path, `FUN_1800150e0`
(`MilanHV_ReadImg`) invokes the same registered callback directly after all
requested live reads succeed. A live read returning `-1` reaches neither route.

## Consequences

- Every completed operation packet can carry the same retained image base.
- The base-refresh marker is one-shot. A temperature/base recalibration marks
  only the next completed sample, causing `GoodixEngineAdapter.dll`
  `FUN_18001f610` to rerun `preprocessor_init` with the newly retained base.
- Reuse is therefore explicit at the hardware boundary; reference acquisition
  is decoupled from individual enrollment/identify probes.

## Image-Base Dispatcher

- Device action `0x0c` selects HAL-context callback slot `+0x180`.
- The slot is loaded at `0x18000e883` and invoked indirectly with the HAL
  context at `0x18000e892`.
- Profile-9 initialization in `FUN_1800162ac` stores
  `FUN_180015c60` (`MilanHV_update_allbase`) in that slot at
  `0x1800163a7..0x1800163ae`.
- Full `deviceInit` invokes action `0x0c` from `0x180020d04`, after action
  `0x0a` and before the final sleep action.
- Action `0x0c` returns the callback's status unchanged. Image-pair rejection
  can therefore return zero while base-valid `+0x232` and image-valid `+0x237`
  remain clear.
- Rejection of the final post-image TX-on FDT comparison has the same dispatcher
  result and validity state. Action `0x0c` does not arm either FDT mode; the
  profile callback has already installed all FDT stores from the first TX-on
  sample before it returns.
- Device action `3` selects slot `+0x1a0`. Profile 9 installs `FUN_180015710`,
  which requests mode `4` and arms FDT-down detection through callback `+0xb0`
  with argument one; it does not require `+0x232` or `+0x237` to be valid.
- Device action `0x10` selects retry slot `+0x1c0` only when both that callback
  and its output argument are non-null. It invokes profile-9
  `FUN_180015760` but discards the callback return, leaving the dispatcher's
  initial zero result unchanged.
- A later false-down action `0` dispatches `MilanHV_Down_procedure`; its
  temperature-event branch can rerun `MilanHV_update_allbase` and establish the
  first valid image reference after an initial rejection.
- Screen-off/on actions `0x17`, `0x11`, `0x15`, `0x16`, D0 power callbacks,
  and normal capture completion do not select slot `+0x180`.

## Profile-9 Direct Mode

Action `0x0e` passes its non-null argument to HAL callback `+0x40`, under the
dispatcher's enabled-context check and critical section. `FUN_18000450c`
installs `FUN_1800059c0` (`Milan_SetMode`) in this slot for profile 9. The
callback consumes a 32-bit mode at argument `+0` and a 16-bit timeout at `+4`:

- A null argument returns `-1`.
- Mode 2 sends category 6, command 0 with no payload through
  `FUN_180017ec0`, using the supplied timeout. It discards the transport
  return, writes HAL dword `+0x1e0 = 2`, and returns zero. The stored mode
  therefore records the request, not confirmed hardware success.
- Mode 4 calls `FUN_180005094` (`Milan_DlCfg`) and returns its status; it does
  not use the supplied timeout or write `+0x1e0`. The helper builds the
  profile-selected 256-byte configuration, applies retained OTP/calibration
  patches, and downloads it through `thunk_FUN_18001aed8` with timeout 500.
  The download makes at most two category-9 command attempts, returning zero
  on transport success or `-1` on final failure. The helper's assembly
  preserves that return through its epilogue.
- Other mode values return zero without issuing a command or changing mode.

Mode 4 does not reset the sensor, rediscover its profile, reread OTP, initialize
GTLS, acquire an image/FDT base, install retained FDT stores through `+0x68`,
or validate the retained image. It restores configuration from the existing
profile/calibration objects rather than proving those objects still describe
the current sensor instance.

Operation-start callback `FUN_180015710` (HAL `+0x1a0`, action 3) requests
mode 4 but discards its return, then calls FDT callback `+0xb0(1)` and returns
that callback's result. In contrast, `MilanHV_update_allbase` checks mode 4's
return before its first acquisition; see `usbinterface-FUN_180015c60.md`.
The initialized `deviceInit` resume route invokes neither mode; its event
completion does not certify that configuration has been downloaded.
