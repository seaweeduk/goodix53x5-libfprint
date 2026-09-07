# Chip ID, Profile, And Algorithm Sensor Type

## Identity And Scope

This contract belongs to the 2.0.310.900 reference driver, with image base
`0x180000000` in both `usbinterface.dll` and `GoodixEngineAdapter.dll`.
USB product IDs do not select profiles. The USB layer calls its profile field
`sensorType` in log messages; that field is not the algorithm sensor-type enum.

## Complete USB Chip Identification

`usbinterface.dll!FUN_180017ef8` is called by `device_enable`
(`FUN_18000e9b0`). It takes a context pointer, reads four bytes from register
zero through `FUN_18001a604(0, buffer, 4, 200)`, and decodes:

```text
chip_id = (b2 << 24) | (b3 << 16) | (b0 << 8) | b1
family = chip_id >> 8
```

The full unsigned chip ID is stored at context `+0x24`. Exact family comparisons
at `0x180017f61..0x180017f7f` select the 32-bit profile at `+0x28`:

| Chip ID predicate | Algorithm sensor type | Profile |
| --- | --- | --- |
| `(chip_id & 0xffffff00) == 0x00220200` | 0 | 0 |
| `(chip_id & 0xffffff00) == 0x00220700` | 6 | 1 |
| `(chip_id & 0xffffff00) == 0x00220800` | 7 | 2 |
| `(chip_id & 0xffffff00) == 0x00220c00` | 12 | 9 |

These are all successful branches of this chip-identification function. The low
byte is ignored for selection. Do not derive algorithm sensor type from the
low byte of `family`: for example, family `0x2202` selects type 0, not type 2.

A null context returns -1 without a read. Failed reads and unrecognized families
call `FUN_18001b6c8(0, status)` and sleep 100 ms before the next attempt. There
are at most six read attempts; exhaustion returns -1. A successful register read
stores the chip ID even when its family is unknown. The identification function
does not clear or assign the profile on failure; successful selection returns 0.

`device_enable` supplies context `0x18005e790`, dispatches profiles 0, 1, and 2
to `FUN_1800112ac`, and profile 9 to `FUN_1800162ac`. After successful profile
initialization it copies the profile to device context `+0x5c`. Identification,
dispatch, profile-initialization, or subsequent `FUN_18000e138` failure assigns
13 (`0x0d`) to both the global profile and device field. Invalid device arguments
or failed device-context lookup return -1 before those assignments. The value
13 is a failure sentinel, not a successful algorithm sensor type or
profile-table entry.

## Profile-To-Type Table

`GoodixEngineAdapter.dll!ppp_param_init` (`0x180002710`) takes a profile index
and accepts unsigned indices 0 through 12. Larger values return `0x81` without
loading a row. Its table is `0x1800e4590`, with 32-byte rows and 32-bit fields:
profile at `+0x00`, rows at `+0x14`, columns at `+0x18`, and algorithm sensor
type at `+0x1c`. It loads those last three fields into globals `0x180218e54`,
`0x180218e68`, and `0x180218e50`, respectively.

| Profile | Algorithm sensor type | Rows | Columns | Chip family selected by this USB identification path |
| --- | --- | --- | --- | --- |
| 0 | 0 | 88 | 108 | `0x002202xx` |
| 1 | 6 | 64 | 176 | `0x002207xx` |
| 2 | 7 | 54 | 176 | `0x002208xx` |
| 3 | 2 | 112 | 132 | Not selected |
| 4 | 1 | 60 | 128 | Not selected |
| 5 | 8 | 88 | 108 | Not selected |
| 6 | 4 | 64 | 176 | Not selected |
| 7 | 63 | 68 | 118 | Not selected |
| 8 | 62 | 96 | 96 | Not selected |
| 9 | 12 | 88 | 108 | `0x00220cxx` |
| 10 | 10 | 64 | 80 | Not selected |
| 11 | 0 | 88 | 108 | Not selected |
| 12 | 17 | 64 | 80 | Not selected |

Rows without a chip-selection branch do not establish any chip-ID mapping or
hardware support. Multiple profiles can share an algorithm sensor type. These
are image dimensions from the native profile table, not physical dimensions.

## Cross-Layer Selection Path

USB `OnGetSensorInfo` (`FUN_1800222a0`) copies device context `+0x5c` to offset
`+0x28` in the 48-byte sensor-info response. Engine `_GetSensorINFO`
(`FUN_18001d3b0`) obtains that response via IOCTL `0x442004`, requiring exactly
48 returned bytes. `EngineAdapterAttach` (`FUN_18001e5b0`) writes the response
at engine context `+0x48`, placing its profile at context `+0x70`.

`_LoadAlgorithmLibrary2` (`FUN_180030c40`) copies that profile into global
`0x1800f5ce0` and passes a local copy to `_Attach_E` (`FUN_18002b240`).
`_Attach_E` calls `ppp_param_init` with the same profile. It does not reinterpret
the profile as an algorithm type.

`preprocessor_init` (`0x1800027b0`) packs the loaded algorithm type into chip
metadata starting at bit 3, with row and column fields starting at bits 14 and
23. The algorithm-side subtype extraction is `(packed >> 3) & 0x3f`; see
`functions/FUN_180037c80.md`. Thus profile 9's table value 12 is the algorithm
sensor type, whereas the USB/engine transport field remains profile 9.

## Current Driver Boundary

The current `goodix_milan_runtime_subtype_for_chip` accepts only `0x00220cxx`
and publishes subtype 12. Recognizing other table entries for detector labels
does not establish support for their calibration, processing, matching, or
device lifecycle.

See `functions/usbinterface-FUN_180004a40.md` for profile-9 OTP admission and
`functions/usbinterface-FUN_180005094.md` for profile-selected configuration.
