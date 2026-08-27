# usbinterface.dll FUN_1800053f0

## Identity

- Address: `0x1800053f0`
- Role: return the most recently parsed profile-9 FDT event vector.
- Installation: `FUN_18000450c` installs it at hardware callback slot `+0x70`
  at `0x18000461a..0x180004621`.

## Contract

Effective ABI:

```c
int get_fdt_event(uint16_t output[12]);
```

A null output pointer returns `-1`. Otherwise it copies exactly 24 bytes from
global event storage `0x180060760..0x180060777` and returns zero. It does not
normalize, transform, mask, or validate the 12 little-endian words.

`FUN_180005b80` writes this storage directly from packet bytes `+4..+27` for
down, up, and reverse IRQs. `Reverse_Occure` and `UP_Occure` call this callback
and independently normalize each unsigned word with `value >> 1` before their
software-anchor comparisons.

## Ordering

For reverse and up IRQs, `FUN_180005538` transforms a private copy into the next
hardware down-arm base without modifying this raw store. The parser/snapshot
order is owned by `usbinterface-profile9-fdt-event-loop.md`; the equivalent base
transform is documented in `usbinterface-FUN_1800048d0.md`. Full acquisition
uses the separate manual-read callback documented in
`usbinterface-FUN_180005420.md` and `usbinterface-FUN_180015c60.md`.
