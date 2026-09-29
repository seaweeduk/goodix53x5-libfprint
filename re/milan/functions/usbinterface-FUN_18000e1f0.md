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
requested live reads succeed. An ordinary live read returning `-1` reaches
neither route.

### Exact retained-frame eligibility and consumption

The dispatcher's common admission requires a nonnull HAL and enabled byte
`+0x204 != 0`, then takes HAL action lock `0x18005e7c8`. Action `0x15`
additionally requires screen byte `0x18005f398 == 1` exactly, tested at
`0x18000e5b7`. It takes cached-frame lock `+0x368`, sets one-press byte
`+0x348 = 1`, and requires both `+0x240 != NULL` and `+0x340 != NULL`.
No size, age, timer-running, requested-mode, wait-mode, image-valid,
power-button, capture-count or pending-WDF-request predicate is added here.
It passes cached size `+0x34c` unchanged to the callback.

The callback at `0x18000e652` observes current reference `+0x248` and current
refresh marker `+0x236`, not snapshots from WOF acquisition. Standard
`CaptureFramedone` (`0x18001fb40`) builds the same fixed-size sample as ordinary
live completion and copies current calibration and health fields. One-press
byte `+0x348 = 1` becomes payload byte `+1`. The cached pixels have already
passed through the ordinary image receive/decode and dynamic-DAC path during
the WOF read; this delivery does not acquire or adjust them again. Engine
preprocessing and quality admission still occur after WBF receives the sample.
See [the sample adapter](FUN_18001f610.md#one-press-sample-flag).

After callback return, action `0x15` clears a nonzero marker, frees/nulls
`+0x340`, and clears `+0x240`, regardless of whether the callback completed a
WDF request. It leaves size `+0x34c`, count `+0x280` and timer `+0x360`
unchanged. Both missing-pointer branches skip consumption. All screen-one
branches reset `+0x348 = 0` at `0x18000e6bd` before releasing `+0x368`.
Thus an old callback surviving cancellation can consume the frame without
delivering it to a request; null callback alone preserves the frame here.

## Consequences

- Every completed operation packet can carry the same retained image base.
- The base-refresh marker is one-shot at callback dispatch. A successfully
  delivered marked sample causes `GoodixEngineAdapter.dll` `FUN_18001f610`
  to rerun `preprocessor_init` with the newly retained base. The dispatcher
  clears the marker even if cancellation prevented WDF delivery; see
  `usbinterface-FUN_18001fb40.md#marker-consumption-is-callback-based`.
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
- Mode 2 calls `FUN_180017ec0(6, 0, 0, NULL, timeout)`. Its
  `FUN_180019ec8` (`ChangeMode`) callee constructs the two-byte payload
  `01 00`, category 6, command 0, checksum selector 1, ACK timeout equal to
  the supplied timeout, no data-response wait, and response selector `0xff`.
  A zero transport result causes one identical retry under the same protocol
  critical section (`0x180063870`); there are at most two attempts.
  `Milan_SetMode` discards the transport return, writes HAL dword
  `+0x1e0 = 2`, and returns zero. The stored mode
  therefore records the request, not confirmed hardware success.
  See `usbinterface-profile9-fdt-event-loop.md` for ACK reception, serialization
  and the separate request-deactivation owner.
- Mode 4 calls `FUN_180005094` (`Milan_DlCfg`) and returns its status; it does
  not use the supplied timeout or write `+0x1e0`. The helper builds the
  profile-selected 256-byte configuration, applies retained OTP/calibration
  patches, and downloads it through `thunk_FUN_18001aed8` with timeout 500.
  The download makes at most two category-9 command attempts, returning zero
  on transport success or `-1` on final failure. The helper's assembly
  preserves that return through its epilogue.
  See `usbinterface-FUN_180005094.md` for the selected template, conditional
  value writes, and configuration-checksum contract.
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

## EC policy bytes and wire encoding

Action `0x11` rewrites its six-byte argument before calling `EcControl`:
`c1 = 1` when device `+0x151 == 1` and screen byte `0x18005f398 == 0`;
otherwise `c1 = incoming_c0`. It then forces `c0 = 0` when requested mode
`+0x1e0 == 2`. This order preserves the selected `c1` even when sleep mode
changes `c0`. Incoming byte one is always overwritten. Delay word `+2`
applies only when resulting `c0 == 0`; timeout word `+4` is the ACK budget.

`EcControl` (`0x18001afec`) serializes three payload bytes `[c0,c1,0]` as
category `0x0a`, command seven, with checksum enabled. The prefixes below omit
the following checksum; `04 00` counts the three data bytes plus checksum:

| Producer and predicates | Resulting prefix |
| --- | --- |
| Capability-one display-off, initial `00 00`, device `+0x151 == 1` | `ae 04 00 00 01 00` |
| Display-off with `+0x151 != 1` | `ae 04 00 00 00 00` |
| Capability-one display-on, initial `01 00`, requested mode not two | `ae 04 00 01 01 00` |
| Same display-on with mode two | `ae 04 00 00 01 00` |
| Screen-on capture admission | `ae 04 00 01 01 00` |
| Screen-off capture admission, device `+0x151 == 1` | `ae 04 00 00 01 00` |
| Deactivation worker, initial `00 00`, screen on or `+0x151 != 1` | `ae 04 00 00 00 00` |
| Same deactivation worker, screen off and `+0x151 == 1` | `ae 04 00 00 01 00` |

Capture admission writes requested mode zero before its EC policy, so its
screen-on route does not inherit the display-on mode-two override. Deactivation
publishes the worker action separately from its mode-two sleep command; it
does not guarantee an all-zero EC payload while screen-off WOF is selected.

At `0x18000e7fb..0x18000e817`, the diagnostic names nonzero `c0` as
`Power Isolate:ON` and zero as `OFF`. At `0x18001b04e..0x18001b08d`,
`EcControl` names nonzero `c1` as `MCU sleep, then set fingerprint FDT`, zero
as `Sleep`. The third payload byte is always zero. These are the host's names
and predicates; they do not establish electrical rail/GPIO polarity or the
firmware implementation of either control.

## Request-Independent Event And Display Dispatch

Except for the separately handled actions 6 and `0x0d`, dispatch requires the
global HAL pointer and enabled byte `+0x204 != 0`, then holds the HAL action
critical section. Actions 0, 1 and 4 select down, up and reverse handlers without
testing capture callback `+0x240`, sleep mode `+0x1e0`, wait state `+0x1fc`,
or the device request's cancellation bytes. Individual handlers own any
additional gates. Up/reverse refresh and rearm therefore do not require an
outstanding capture request; the ordinary down live-image path separately tests
the retained capture callback, which cancellation can leave installed.

Action 7 (`0x18000e35f..0x18000e3d7`) requires a nonnull argument, stops and
clears a nonnull timer at `+0x238`, then invokes arm callback `+0xb0` with the
argument byte. It does not test mode, image/base validity or capture ownership.
For profile 9, byte one arms down and zero arms up through `0x180005a60`.

Action `0x16` (`0x18000e47f..0x18000e5ad`) requires mode `+0x1e0 == 0`.
It sends EC control with byte zero 0, byte one 1 only when device context
`+0x151 == 1`, and timeout 200. Regardless of the EC result, a nonnull arm
callback receives zero when retained wait state is `0xf1`, otherwise one.
Mode 2 skips the whole branch. It neither acquires bases nor installs a capture
callback. [Display notifications](usbinterface-FUN_1800174a0.md) select between
this mode-gated route and the capability-one action-7 route.

The current counterparts for actions 0/1/4 are foreground and idle modes of
`drivers/goodix53x5/device/scan.c:goodix_scan_coordinator_handler`.
`goodix_scan_start_service` enters request-independent event handling;
`device/transport.c:goodix_rx_cell` applies parser mutations and publishes the
coalescing `pending_fdt` notification independently of a foreground request.
Action 3 maps to `GOODIX_SCAN_COORD_RESTORE_CONFIG`,
`device/commands.c:goodix_cmd_restore_config`, and the existing down-arm path.
There is no request-independent display action-7/action-`0x16` owner; system
suspend maps action `0x11` (see
[display notifications](usbinterface-FUN_1800174a0.md#current-source-map)). See
`usbinterface-profile9-fdt-event-loop.md#current-source-mapping` for servicing,
handoff and power ownership.
