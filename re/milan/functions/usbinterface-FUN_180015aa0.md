# usbinterface.dll FUN_180015aa0

## Identity And Ordering

- Address: `0x180015aa0`; logged name: `MilanHV_UP_procedure`.
- Profile-9 up wrapper; null HAL returns `-1`.
- Calls `FUN_1800149c4` for the ordinary up/reference decision and ignores its
  return. Then, if HAL health-enabled `+0x2e1 != 0` and retained image-valid
  `+0x237 == 1`, calls `FUN_180011b9c(HAL+0x30c, 0, dword[HAL+0x308])`.
- Health eligibility does not test capture-request presence, callback presence,
  requested mode, or whether this up refreshed the reference. The retained
  operation selector is supplied even without a request.
- A nonzero health return logs `gf_broken_check error, but can't affect the
  normal flow.` at level five. Both success and failure signal event `+0x300`.
- Calls the installed FDT-down arm callback `+0xb0` with argument one and
  returns that callback's result. Health completion therefore precedes down-arm.
- The later platform branch, when power-button byte `+0x358 == 0` and capability
  global `0x1800e2120 == 1`, frees/nulls cached frame `+0x340` under critical
  section `+0x368`. It does not gate ordinary health or down-arm.

See [the ordinary UP decision](usbinterface-FUN_1800149c4.md),
[health history and consumers](usbinterface-FUN_180011b9c.md), and
[FDT worker dispatch](usbinterface-profile9-fdt-event-loop.md).

## Current Source Mapping

`drivers/goodix53x5/device/scan.c:GOODIX_SCAN_COORD_UP_HEALTH` selects
`device/health.c:goodix_health_start_pair` with `GOODIX_HEALTH_PAIR_UP` only
for `event_type == GOODIX_FDT_EVENT_UP && hardware_reference != NULL`.
It follows the reference decision/refresh continuation, including ordinary
refresh failure, and precedes `GOODIX_SCAN_COORD_REARM_DOWN`. FDT-base validity,
requested mode and foreground-request presence do not gate this pair. BASE is
the separate pair inside all-base acquisition, so an eligible UP refresh can
execute both. The health child owns evaluation and completion; ordinary
unavailable measurement still continues to down-arm, while terminal host errors
use joined failure handling. Sensor-health history is separate from the
engine's broken-pixel classification. The capability-dependent cached-frame
cleanup at native `+0x340/+0x368` has no current Linux WOF owner.
