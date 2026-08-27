# usbinterface.dll FUN_1800162ac

## Identity

- Address: `0x1800162ac`
- Role: initialize the Milan HV HAL context and profile-9 callback table.

## Profile-9 Initialization

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
- Allocates retained image-base buffer `+0x248` with `malloc(rows * columns * 2)`.
  The allocation is not initialized here.
- Allocates the auxiliary buffer at `+0x258` and retained FDT-calibration
  storage at `+0x268` after setting their recorded sizes.

## Initial-Failure Consequence

The first action-`0x0c` acquisition therefore starts with `+0x232 == 0` and
`+0x237 == 0`. If image-pair validation rejects, `MilanHV_update_allbase` does
not write `+0x248`; its contents remain unspecified and are not a valid retained
reference. The common postlude still replaces `+0x268` and programs the
profile-9 FDT bases.
