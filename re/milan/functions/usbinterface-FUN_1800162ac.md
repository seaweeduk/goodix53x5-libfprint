# usbinterface.dll FUN_1800162ac

## Identity

- Address: `0x1800162ac`
- Role: initialize the Milan HV HAL context and profile-9 callback table.

The corresponding profile-0 initializer is `FUN_1800112ac`; its separate
callback layout, geometry, and calibration/acquisition contract are owned by
[Profile 0 USB Contract](../PROFILE0-USB-CONTRACT.md).

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

## Initial-Failure Consequence

Failures after the enabled publication are not transactional. In particular,
the image and auxiliary `malloc` null branches at `0x18001653e` and
`0x180016555` reach the common return without assigning a new nonzero status;
the final FDT allocation is stored without a null check. These paths can return
the earlier successful profile-open status with incomplete storage. They do not
establish a valid image/reference. Enabled teardown remains owned by
`FUN_1800160a0`, whose non-null buffer checks allow partial allocation cleanup.

The first action-`0x0c` acquisition therefore starts with `+0x232 == 0` and
`+0x237 == 0`. If image-pair or final TX-on FDT validation rejects,
`MilanHV_update_allbase` does not write `+0x248`; its contents remain unspecified
and are not a valid retained reference. The common postlude still replaces
`+0x268` and programs the profile-9 FDT bases.
