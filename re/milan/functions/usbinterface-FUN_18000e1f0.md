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
