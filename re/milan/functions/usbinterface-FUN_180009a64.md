# usbinterface.dll FUN_180009a64

## Identity And Boundary

- Address/body: `0x180009a64..0x180009b47` (inclusive).
- Role: expand packed raw12 bytes into consecutive unsigned 16-bit samples.
- Profile-9 / sensor-type-12 caller: `FUN_180007968`, call at `0x180007bad`.
- Arguments (Windows x64): `RCX` input bytes, `RDX` caller-owned output words,
  `R8D` unsigned input byte count. No callees, global accesses, allocation or
  meaningful return value; source and destination are separate caller buffers.
- Related current owner: `device/image.c:goodix_device_decode_image`.

## Packing And Iteration

For group `g`, let `b0..b5` be input bytes at `6*g..6*g+5`. The four words at
output indices `4*g..4*g+3` are written in this order:

```text
p0 = 256 * (b0 & 15) + b1
p1 = 16 * b3 + (b0 >> 4)
p2 = 256 * (b5 & 15) + b2
p3 = 16 * b4 + (b5 >> 4)
```

All byte loads are unsigned. Nibble extraction precedes the addition, and the
addends occupy disjoint bit positions. Each result is in `0..4095` and is stored
as a little-endian word without scaling, clamping, border treatment or metadata.
Each input bit contributes to exactly one output bit; the high four bits of
every output word are zero. The function never writes the input.

`EDI` starts at 2 (output index plus two) and `R11D` at 3 (input offset plus
three). Each iteration advances them by 4 and 6 respectively. After the fourth
store, the unsigned comparison at `0x180009b2a` continues while the next input
offset is below the byte count. The entry zero-count branch returns immediately.
For the complete profile-9 frame, the count is `0x37b0` (14,256), giving exactly
2,376 groups and 9,504 words (19,008 bytes, `0x4a40`). The first group reads bytes
0..5 and writes words 0..3; the last reads bytes 14,250..14,255 and writes words
9,500..9,503. No row permutation or dependency between groups exists.

## Caller Ownership And Publication

`FUN_180007968` requires non-null status and message pointers and an installed
output pointer at `DAT_180060708`. It owns separate temporary pre-GEA and
post-GEA buffers. Successful reply processing and the subtype-selected CRC
check precede conversion; their contract is documented in
[`usbinterface-FUN_180024940.md`](usbinterface-FUN_180024940.md).

At `0x180007b97..0x180007bad`, the converter receives the post-GEA buffer, the
installed output pointer, and the published post-GEA byte count minus four CRC
bytes. For a valid complete type-12 frame this is exactly 14,256 input bytes;
the output owner must provide 9,504 words. The converter does not receive an
output capacity or independently impose sensor dimensions. Its complete-frame
contract assumes the caller supplies the correct count and adequate separate
storage, rather than deriving either from the pixel data.

After conversion the caller invokes `FUN_18000dd84` with its local value 8,
publishes zero status to the caller and `DAT_180060cc0`, and frees both temporary
buffers. It retains the installed output buffer. Earlier reply or CRC failures
do not enter this converter.
