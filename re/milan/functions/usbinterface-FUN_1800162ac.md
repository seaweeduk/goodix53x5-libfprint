# usbinterface.dll FUN_1800162ac

## Identity

- Address: `0x1800162ac`
- Role: initialize the Milan HV HAL context and profile-9 callback table.

## Profile-9 Initialization

Entry requires a non-null device wrapper, non-null wrapper field `+0x68`,
HAL-enabled byte `DAT_1800617f4 == 0`, and a non-null framework context returned
through WDF table slot `+0x3d8`. Failure of any of these gates returns `-1`
before publishing the profile context. An already-enabled HAL is not reopened
or implicitly torn down by this callback.

Established callback assignments used by the profile-9 refresh/event contract
include:

- Installs `MilanHV_update_allbase` (`FUN_180015c60`) at HAL callback slot
  `+0x180`, `MilanHV_Down_procedure` at `+0x190`, and the operation-start
  callback `FUN_180015710` at `+0x1a0`.
- Installs the up-event wrapper `FUN_180015aa0` at `+0x188`, the reverse-event
  wrapper `FUN_180015a60` at `+0x198`, retry capture `FUN_180015760` at `+0x1c0`
  (`0x1800163fb..0x180016402`), and close callback `FUN_1800160a0` at `+0x1c8`.

The remaining initialization contract is:

- Calls `GxFNHV_MilanOpen` for profile 9 before marking the HAL enabled.
- Clears the global bytes covering base-valid `+0x232/+0x233`, one-shot marker
  `+0x236`, and image-base-valid `+0x237` before allocating image storage.
- The word store at `0x18001648e` also clears persisted-base byte `+0x231`.
  The enabled byte is incremented before these validity clears and before the
  retained-image allocations.
- Writes anchor-empty byte `+0x338` (`DAT_180061928`) to one at
  `0x1800164b0`. It does not clear the twelve anchor words at `+0x320`, so a
  reenabled HAL can retain their old bytes while the empty flag makes them
  inactive. First up after initialization bypasses anchor comparison; first
  qualifying reverse can seed the anchor. See `usbinterface-FUN_1800149c4.md`.
- Allocates retained image-base buffer `+0x248` with `malloc(rows * columns * 2)`.
  The allocation is not initialized here.
- Allocates the auxiliary buffer at `+0x258` and retained FDT-calibration
  storage at `+0x268` after setting their recorded sizes.

## Calibration And Reference Lifetimes

The constructor is reached through `device_enable`, not ordinary capture
requests. Full initialization subsequently dispatches action 9 to
`Milan_CheckSensor` (`0x180004a40`), which selects current live DAC `+0x312`
and reference DAC `+0x310` from verified OTP or their fallback rules and seeds
the dynamic helper through `0x180007c74`. That seed helper writes temperature
and default DAC only; it does not clear the four adjustment-history words.
The constructor/profile-open sequence likewise does not clear those words.
See [the calibration owner](usbinterface-FUN_180004a40.md) and
[the dynamic-DAC owner](usbinterface-FUN_180007c84.md).

An initialized D0-entry worker bypasses this constructor and action 9, retaining
current DAC, its seed, history, image/FDT allocations, validity bytes and marker.
A full enable instead creates new allocations and clears the bytes below even
when the DLL's adjustment history survives:

| HAL field | Constructor | Saved-base loader | Admitted all-base acquisition |
| --- | --- | --- | --- |
| `+0x231` persisted-file flag | Zero | One on admitted file; zero on missing file | No write |
| `+0x232/+0x233` base-valid pair | Zero | No write | Both one |
| `+0x236` one-shot setup marker | Zero | No write | No write; temperature-event caller sets it on admission |
| `+0x237` image-valid | Zero | No write | One |
| `+0x248` image allocation | Allocated, unspecified bytes | Copy file image | Copy admitted TX-on image |
| `+0x338` drift-anchor-empty | One; anchor bytes retained inactive | No write | No write; selected event callers own clearing |

The saved file and fresh acquisition contracts are owned by
[deviceInit](usbinterface-FUN_180020970.md) and
[MilanHV_update_allbase](usbinterface-FUN_180015c60.md). File presence is
not a replacement for image-valid state.

Current source mapping: `device/session.c:goodix_open_ssm_handler` and
`device/calibration.c:goodix_device_parse_otp` own Linux calibration selection;
`GoodixDynamicDacState` and `calib.dac_h` split history/default from current DAC.
Every cold open/reinitialization parses freshly read OTP and reseeds
`calib.dac_h` and `dynamic_dac.default_dac`. The four adjustment-history words
belong to module-static `device/scan.c:goodix_dac_history`, used under
`goodix_dac_history_lock` by `goodix_capture_ssm_handler`; cold initialization
does not clear them. `self->dynamic_dac` holds the device default and last-live
history snapshot. The complete mapping is in `usbinterface-FUN_180007c84.md`.
These owners have no cached OTP/bootstrap or serialized DAC-resume route.
`device/base.c:goodix_base_ssm_handler` owns fresh reference admission and
`GoodixProfile9FdtState` owns the FDT validity/anchor state. These are separate
owners rather than a retained native HAL allocation.

### FDT Stores At The Next Command Boundary

The profile-open callback installs setter `0x180005950` at `+0x68`, arm
`0x180005a60` at `+0xb0`, raw-event getter `0x1800053f0` at `+0x70`,
and manual-FDT getter `0x180005420` at `+0x160`. These stores have different
producer/consumer lifetimes:

| Address / bytes | Producer | Next consumer |
| --- | --- | --- |
| `0x180060718`, 24 | Setter and selector-3 manual response | Successful manual getter output; not an arm parameter |
| `0x180060730`, 24 | Setter, up/reverse parser transform, reverse rejection restoration | Down-arm command's complete base argument |
| `0x180060748`, 24 | Setter and down parser's touch-dependent transform | Up-arm command's complete base argument |
| `0x180060778`, 24 | Setter and reverse parser's pre-update down-base copy | Manual-FDT command argument; reverse getter returns each word's high byte |
| `0x180060760`, 24 | Each accepted down/up/reverse packet | Selected event handler's raw input |
| `0x180060790`, 2 | Down/reverse packet touch word | Down parser's up-base transform; up does not consume the retained touch word |

The setter performs only four software copies, not sensor programming. Down
and up arm requests carry `[0x0c,1,down[24]]` and `[0x0e,1,up[24]]` through
`0x180019ec8:0x18001a272..0x18001a36a`. Manual requests carry TX selector
`0x0d`/`0x8d`, byte one, and all 24 bytes from `0x180060778`. They reset
response readiness before sending; only a completed manual response permits
`0x180005420:0x180005497..0x1800054c8` to copy the manual result. The event
parser replaces the complete raw vector before signalling its worker.

At a boundary with no pending worker notification or response, the previous
manual-result and raw-event bytes are not inputs to a new arm. A newly
received event or successful manual reply supplies them before use. The
down/up/manual command stores and active drift anchor are retained inputs,
however, and cannot all be reconstructed from the last admitted FDT sample:
events and rejected refresh postludes can have changed them independently.
Pending event data is a different owner from the one-shot image-setup marker.
See [the event loop](usbinterface-profile9-fdt-event-loop.md) for parser order
and coalescing, and [deviceInit](usbinterface-FUN_180020970.md) for retained
image/configuration/session boundaries.

## Initial-Failure Consequence

Failures after the enabled publication are not transactional. In particular,
worker-event creation failure at `0x1800164ce` jumps to `0x180016569` with
the earlier successful profile-open status unchanged, just as the following
buffer-allocation failures do. The enabled byte was already incremented at
`0x180016475`. By contrast, failure of any of the eleven earlier auto-reset
response events explicitly sets `EBX = -1` at `0x18001656f` and enters their
cleanup loop. A zero constructor return does not prove a usable worker event.

For the retained buffers,
the image and auxiliary `malloc` null branches at `0x18001653e` and
`0x180016555` reach the common return without assigning a new nonzero status;
the final FDT allocation is stored without a null check. These paths can return
the earlier successful profile-open status with incomplete storage. They do not
establish a valid image/reference. Enabled teardown remains owned by
`FUN_1800160a0`, whose non-null buffer checks allow partial allocation cleanup.

The first action-`0x0c` acquisition therefore starts with `+0x232 == 0` and
`+0x237 == 0`. If image-pair or final TX-on FDT validation rejects,
`MilanHV_update_allbase` does not write `+0x248`; its contents remain unspecified
unless the preceding saved-base loader copied a file image. In either case
image-valid remains clear: restored bytes alone are not a valid admitted
reference. The common postlude still replaces `+0x268` and programs the
profile-9 FDT bases.
