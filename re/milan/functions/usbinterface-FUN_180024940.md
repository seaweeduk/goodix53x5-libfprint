# usbinterface.dll FUN_180024940

## Identity

- DLL: `usbinterface.dll`
- Address/body: `0x180024940..0x1800251e1`
- Role: verify and decrypt one alternating AES/GEA GTLS sensor reply.
- Profile-9 / subtype-12 caller path:
  `FUN_1800048a0 -> FUN_180007968 -> FUN_180024940`.

## Length Handling

- The function rejects an input shorter than 8 bytes and requires the embedded
  message length at input offset `+4` to equal the supplied input length.
- It reconstructs the alternating passthrough/AES blocks, verifies the HMAC,
  strips the five-byte GEA prefix, and copies the remaining bytes to both output
  buffers.
- The GEA primitive is `FUN_180024790`. Its word count is
  `(message_length - 0x31) >> 1`, so an odd final data length is rounded down to
  complete 16-bit words. The copied trailing byte remains unchanged; the
  primitive neither reads nor writes beyond it.
- `FUN_180007968` then checks a subtype-selected fixed CRC span. For profile 9 /
  subtype 12, `FUN_180009b88` selects `0x37b0` raw12 bytes, and the later image
  conversion writes the fixed `108 * 88 * 2 == 0x4a40` output consumed by the
  hardware and engine layers.

## Implication

Native's GEA word loop is memory-safe for an odd byte count, but native does not
explicitly reject the odd count at the GTLS boundary. Its surrounding profile-9
pipeline assumes a fixed-size image and validates a CRC at the fixed subtype-12
offset. This is evidence for bounding the primitive independently; it is not a
reason for Linux to accept a malformed half-word image instead of rejecting it.

Confidence: high for the word-count arithmetic, unchanged trailing byte, and
profile-9 fixed CRC/image sizes. The behavior of deliberately malformed GTLS
frames has not been exercised dynamically.
