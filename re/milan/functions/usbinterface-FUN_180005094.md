# usbinterface.dll FUN_180005094

## Identity And Boundary

- Address: `0x180005094..0x1800051fa`
- Logged name: `Milan_DlCfg`
- Role: copy the profile-selected sensor configuration, apply retained
  calibration patches, and pass the complete 256-byte buffer to `Dlcfg`.
- Caller: `FUN_1800059c0` (`Milan_SetMode`), mode 4, at `0x180005a23`.
- Current counterparts: `goodix_device_get_default_config`,
  `goodix_device_patch_config`, and `goodix_device_fix_config_checksum` in
  `device/calibration.c`; download handoff `goodix_cmd_upload_config`.

The owner uses the profile context published at `0x1800606d0`, global TCODE
word `0x1800606fa`, and packed-DAC-valid byte `0x1800606fe`. It does not read
OTP, recalculate calibration, or mutate those inputs. Its configuration is a
private 256-byte stack buffer at post-prologue `RSP+0x40`; no pointer to that
buffer is retained by this owner after return.

## Selected Template

At `0x180005105..0x180005110`, the owner passes context dword `+0x228` to
`FUN_1800180f8`. That field is the profile, not the USB ID or sensor type
number. Type-12 chip family `0x220c` selects profile 9 in `FUN_180017ef8`;
`FUN_1800162ac:0x180016369..0x18001636c` copies the selected profile from
argument `+0x28` into context `+0x228`.

`FUN_1800180f8:0x180018103..0x180018165` copies exactly 256 bytes from
`0x18005f3a0 + profile * 0x100`. Profile 9 selects `0x18005fca0`. It then
repairs the destination checksum without changing the source table. A null
destination returns false; the owner supplies its non-null stack buffer.

The compiled profile-9 template's trailer at `0x18005fd9e` is LE16
`0x4f61`. That stored value is not its repaired checksum: the copier replaces
the destination trailer with `0x0f53` before any calibration patches. The
compiled trailer is therefore not an authoritative download checksum.

Byte zero is `0x40`. Section `s` has unsigned byte base and size at
`config[1+2*s]` and `config[2+2*s]`. Profile-9 section ranges are:

| Section | Base | Size |
| --- | --- | --- |
| 0 | `0x11` | `0x6c` |
| 1 | `0x7d` | `0x28` |
| 2 | `0xa5` | `0x28` |
| 3 | `0xcd` | `0x1c` |
| 4 | `0xe9` | `0x10` |
| 5, 6, 7 | `0xf9` | zero |

Each entry is a little-endian 16-bit tag followed by a little-endian 16-bit
value. Entry starts advance by four bytes. The selected sections have
four-byte-multiple lengths, do not overlap, and end before the checksum.

## Calibration Writes

The calls occur in this order:

| Condition in owner | Helper | Section | Tag | Value |
| --- | --- | --- | --- | --- |
| TCODE word nonzero | `FUN_180018420` | 4 | `0x005c` | TCODE |
| same | `FUN_180018320` | 2 | `0x005c` | TCODE |
| same | `FUN_1800185f4` | 3 | `0x005c` | TCODE |
| context word `+0x318` nonzero | `FUN_180018264` | 2 | `0x0082` | `uint16((delta_down << 8) \| 0x80)` |
| packed-DAC-valid byte nonzero | `FUN_18001819c` | 2 | `0x0220` | `uint16((dac_low << 4) \| 8)` |
| same | `FUN_180018528` | 3 | `0x0220` | same |

The TCODE guard is at `0x180005115..0x18000511f`, the delta guard at
`0x18000515d..0x180005167`, and the unsigned byte DAC guard at
`0x180005186..0x18000518c`. Shift and OR operations use 16-bit registers,
discarding high bits before the helper receives the value.

The helpers scan every entry in the selected section, replacing every matching
tag, not just the first. Tags, section descriptors, and unrelated values are
unchanged. TCODE helpers also support an optional old-value output, overwritten
on every matching tag; this owner passes null. They leave matching values
unchanged when their replacement argument is zero. Both DAC helpers likewise
skip zero replacement arguments, although the owner's `OR 8` makes that case
impossible. The delta helper has no replacement-value zero guard. A non-null
buffer returns true even when no tag matches; null returns false without
accessing the buffer. The owner does not inspect these helper returns.

The exact value offsets and compiled template defaults for profile 9 are:

| Field | Value Offsets | Initial LE16 Values |
| --- | --- | --- |
| TCODE, sections 2/3/4 | `0xb7`, `0xd3`, `0xef` | `0x0100`, `0x0080`, `0x0080` |
| Delta down, section 2 | `0xc7` | `0x0c80` |
| Low DAC, sections 2/3 | `0xcb`, `0xe7` | `0x0d88`, `0x0d88` |

No other configuration value is patched by this owner. In particular,
section-3 `0x0082`, section-1 TCODE, and unrelated section-0 register values
are not additional calibration outputs here.

## Producer Constraints

Successful `Milan_CheckSensor` (`FUN_180004a40`) publishes the serialization
inputs. See `usbinterface-FUN_180004a40.md` for the calibration owner.

- `0x180004cd1..0x180004cf5` sets global TCODE to zero for zero OTP byte 23,
  otherwise to the unsigned byte plus one. The resulting domain is zero or
  `2..256`. Zero is an explicit successful-calibration case, not an error;
  configuration serialization preserves all three template TCODE values then.
- `0x180004e06..0x180004e15` tests OTP bytes 17, 22, and 31. If all are
  nonzero, `0x180004e50` stores the packed low DAC and `0x180004e6c` sets
  the packed-DAC-valid byte to one. Otherwise `0x180004e96` stores low DAC
  `0xd0` and `0x180004e9d` clears that byte. The clear-byte case preserves
  both template values `0x0d88`; it does not encode the fallback `0xd0`.
- Packed low DAC can itself equal `0xd0`, so the numeric low-DAC value alone
  does not identify the validity byte. Its value and provenance are separate
  inputs to this serialization contract.
- The initialized delta-down producer at `0x180004cfc..0x180004dd1` emits
  `6..37` for nonzero five-bit difference and 13 for zero difference. Thus
  the owner's delta-down guard is always taken after successful initialized
  profile-9 calibration.

## Checksum And Complete Output

The template copier and every invoked patch helper recompute the checksum:

```text
sum = uint16(0xa5a5 + sum(LE16(config + 2*i), i=0..126))
LE16(config + 0xfe) = uint16(-sum)
```

The copier's instructions are `0x180018167..0x180018193`. Each helper uses
the same 127-word sum, 16-bit additions and negation, and low-byte/high-byte
stores. The previous checksum is excluded. Thus the complete 128-word buffer
sums to `0x5a5b` modulo 65536. Repeated intermediate repairs have no effect on
the final result compared with one repair after all disjoint value writes.

For the selected template, the final configuration is exactly the copied
template with the conditionally selected values in the table above and the
checksum replaced by this formula. Value offsets are odd; checksum words
start at even offsets. Changing both low-DAC values from `0x0d88` to
`0x0d08` would change each containing checksum word by `-0x8000`, leaving
the checksum unchanged modulo 65536 despite changing two configuration bytes.

At `0x1800051c5..0x1800051d5`, the complete buffer passes to
`thunk_FUN_18001aed8` with length `0x100` and timeout 500. There are no
subsequent owner writes to configuration bytes. `FUN_18001aed8` passes that
same pointer and length to its category-9, command-0 consumer at
`0x18001af27`; its one failure retry at `0x18001af51` uses the same buffer.
It performs no configuration rewriting or additional configuration-checksum
repair. Final download failure returns `-1`, success zero; `Milan_DlCfg`
preserves that result through its epilogue.

See `usbinterface-FUN_18000e1f0.md` for mode dispatch and
`usbinterface-FUN_180015c60.md` for the base-acquisition caller.
