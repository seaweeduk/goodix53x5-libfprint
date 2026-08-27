# usbinterface.dll FUN_1800048d0

## Identity

- Address: `0x1800048d0`
- Role: convert one 12-area raw FDT vector into the 24-byte programmed base
  representation.
- Profile-9 ownership: `FUN_18000450c` installs this function at hardware
  callback slot `+0x88`. `MilanHV_update_allbase` is the slot's profile-9
  consumer.
- Current mapping: `goodix_device_generate_fdt_base()` in
  `drivers/goodix53x5/device/calibration.c`.
- Native callees: none.

## Contract

The function takes one writable pointer to 12 consecutive little-endian
unsigned 16-bit samples. A null pointer returns `-1`. A non-null pointer is
transformed in place and returns zero.

For each sample `s`, in order from area 0 through area 11, the stored word is:

```text
((s & 0xfffe) * 0x80 + (s >> 1)) mod 0x10000
```

Equivalently, with `q = floor(s / 2)`, the result is `(q * 0x101) mod
0x10000`; adjacent even/odd inputs therefore produce the same output.

The implementation zero-extends the input word, multiplies the even part in a
32-bit register, adds only the low 16-bit halves with `ADD DX,R8W`, and stores
`DX`. There is no signed input, saturation, range check, or widening of the
stored result. The loop counter starts at 12 and the pointer advances by two
bytes each iteration.

## Producers And Consumers

`MilanHV_update_allbase` invokes this callback in its common postlude on the
first TX-on manual FDT reading. The reading remains the input even when later
FDT or image-pair validation prevents image-reference publication. On callback
success the caller copies the transformed bytes into retained FDT-calibration
storage `+0x268`, then passes the same bytes to callback slot `+0x68`.
Profile-9 `FUN_180005950` copies that value into all four hardware-profile
stores: primary `0x180060718`, down-arm `0x180060730`, up-arm `0x180060748`,
and manual-FDT `0x180060778`.

Manual responses and MCU events both carry 12 little-endian words after the
four-byte IRQ/touch header. `FUN_180005b80` copies the 24 sample bytes without a
sample-value mask or rejection predicate. Its manual-response path supplies
the raw TX-on input used here. Its up and reverse event paths instead call
`FUN_180005538`, which applies the same 12-word additive, modulo-16-bit
transform to a private copy and replaces the down-arm store.

The down and up commands consume `0x180060730` and `0x180060748` respectively.
`FUN_180005420` owns manual-command consumption of `0x180060778`.

## Related Functions

- `usbinterface-FUN_180015c60.md`: complete base acquisition and common
  postlude.
- `usbinterface-FUN_180005420.md`: manual-command consumer of the retained
  manual-FDT base.
- `usbinterface-profile9-fdt-event-loop.md`: MCU parser, event transforms, and
  rearming.
