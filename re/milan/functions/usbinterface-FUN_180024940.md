# usbinterface.dll FUN_180024940

## Identity

- DLL: `usbinterface.dll`
- Address/body: `0x180024940..0x18002522c`
- Role: verify and decrypt one alternating AES/GEA GTLS sensor reply.
- Profile-9 / sensor-type-12 caller path:
  `FUN_1800048a0 -> FUN_180007968 -> FUN_180024940`.
- Profile-9 selection in `FUN_1800162ac` calls `FUN_18000450c`, which installs
  `FUN_1800048a0` at device operations offset `+0x140`.

## Inputs And Reconstruction

- Arguments are context, message, uint32 message length, pre-GEA output and
  uint32 capacity/length pointer, post-GEA output and uint32 length pointer.
  All six pointers must be non-null. The first output capacity must be at least
  `message_length - 0x28`; the second capacity is not checked.
- Context offsets are AES key `+0x88` (16 bytes), IV `+0x98` (16 bytes), HMAC
  key `+0xa8` (32 bytes), receive counter `+0xd0` (uint32), and state `+4`.

- The function rejects an input shorter than 8 bytes and requires the embedded
  message length at input offset `+4` to equal the supplied input length.
- The first message dword is not checked here. The alternating reconstruction
  copies `0x3a7` bytes at message `+8`, then thirteen `0x3f0` blocks, decrypting
  the odd blocks with `FUN_180026b50(1, iv, key, ...)`. Each AES call restarts
  with the context IV and must succeed with exactly `0x3f0` output bytes.
  The AES wrapper selects padding mode 4 through `FUN_180028570`: no padding
  writer and a length-preserving padding reader (`FUN_180028210`).
  The final copy starts at message `+0x36df` and includes the trailing HMAC.
  These fixed accesses require a sufficiently large message independently of
  the explicit eight-byte minimum check; that check alone does not guarantee
  valid reconstruction accesses.

## Authentication And Publication

- Let `P = message_length - 0x28`. HMAC-SHA256 (`FUN_180027430`) covers the
  little-endian four-byte counter followed by the final `min(P, 0x400)` bytes
  of the reconstructed payload. The digest is compared against its following
  32 bytes at `0x180024e62..0x180024e89`.
- HMAC mismatch returns `0xffbffcfe` without changing the counter, either output
  buffer, or either length. AES/HMAC primitive failure also returns before
  counter advancement or output publication.
- `INC dword ptr [R12 + 0xd0]` at `0x180024ed8` advances the counter modulo
  `2^32` immediately after authentication. No later failure rolls it back.
- After checking `P > 5`, the function copies `P - 5` bytes from reconstructed
  payload `+5` to the pre-GEA output and sets its length. Only then does it
  require context state `5` (`0x180024fdd..0x180025025`). State failure returns
  `0xff8ffffc`, retaining that first output and the advanced counter but leaving
  the second output and length unchanged.
- In state 5 it copies the same bytes to the post-GEA output, publishes its
  length, and GEA-decrypts the data in place, excluding the last four CRC bytes.
  Success returns zero. Both published lengths include the unchanged CRC.
- The GEA primitive is `FUN_180024790`. Its word count is
  `(message_length - 0x31) >> 1`, so an odd final data length is rounded down to
  complete 16-bit words. The copied trailing byte remains unchanged; the
  primitive neither reads nor writes beyond it.
- `FUN_180007968` then checks a subtype-selected fixed CRC span. For profile 9 /
  sensor type 12, `FUN_180009b88` selects `0x37b0` raw12 bytes, and the later image
  conversion writes the fixed `108 * 88 * 2 == 0x4a40` output consumed by the
  hardware and engine layers.

- CRC is checked on the pre-GEA buffer by `FUN_180009b88` with mode zero and
  the subtype byte at device context `+0x1ec` (9 for this path). It compares
  CRC32-MPEG2 of the fixed `0x37b0` bytes to
  bytes at that offset in order `b0*0x100 + b1 + b2*0x1000000 + b3*0x10000`.
  Failure sets caller status to `0xffffffff` and does not call the downstream
  image conversion `FUN_180009a64`. The authenticated counter remains advanced;
  both temporary buffers are freed. GTLS failure likewise prevents conversion.
