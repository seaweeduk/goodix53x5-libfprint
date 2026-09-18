# usbinterface.dll FUN_180024940

## Identity

- DLL: `usbinterface.dll`
- Address/body: `0x180024940..0x18002522c`
- Role: verify and decrypt one alternating AES/GEA GTLS sensor reply.
- Profile-9 / sensor-type-12 caller path:
  `FUN_1800048a0 -> FUN_180007968 -> FUN_180024940`.
- Profile-9 selection in `FUN_1800162ac` calls `FUN_18000450c`, which installs
  `FUN_1800048a0` at device operations offset `+0x140`.
- Current mapping: `usbinterface.dll!0x180024940` maps to
  `drivers/goodix53x5/device/crypto.c:goodix_crypto_gtls_decrypt_sensor_data`;
  the `0x180007968` caller's CRC/decoded-cache/status publication is split across
  that helper, `device/image.c:goodix_device_decode_image`, and
  `device/transport.c:goodix_rx_cb`. Reader error history and restart are owned
  by [the handshake note](usbinterface-FUN_180025400.md).

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

The AES wrapper `FUN_180026b50` uses a stack cipher descriptor and propagates
nonzero returns from cipher setup (`028620`), padding selection (`028570`), key
setup (`0285c0`), IV setup (`0284f0`), reset (`0284d0`), update (`0286c0`) and
finish (`0282b0`); its common exit destroys the descriptor through `028430`.
Cipher lookup `028480(5)` failure returns `-0x400103`. Output length includes the
finish length only after successful finish. The current AES-wrapper counterpart
is `device/crypto.c:goodix_crypto_aes_cbc_decrypt`. HMAC wrapper `027430` selects
SHA256 with `0280d0(6)`, returns `-0x400304` on missing digest metadata, and
propagates the return from `027c30`. These wrapper-level return checks do not
establish that every lower-level primitive checks every internal failure.
Cipher setup `028620` zeroes the descriptor and calls the selected cipher's
allocator. A null result returns `-0x6180`, propagated by the wrapper and sensor
reply owner before authentication/counter publication. The descriptor metadata
is installed only after allocation succeeds. HMAC allocation and unchecked
inner digest-call behavior are described in the
[handshake owner](usbinterface-FUN_180025400.md#server-identity-and-key-publication).

- Let `P = message_length - 0x28`. HMAC-SHA256 (`FUN_180027430`) covers the
  little-endian four-byte counter followed by the final `min(P, 0x400)` bytes
  of the reconstructed payload. The digest is compared against its following
  32 bytes at `0x180024e62..0x180024e89`.
- HMAC mismatch returns `0xffbffcfe` without changing the counter, either output
  buffer, or either length. A nonzero AES-wrapper return or a successful return
  with length other than `0x3f0` is rejected at `0x180024d3f..0x180024d55`.
  A nonzero HMAC-wrapper return is rejected at `0x180024e11..0x180024e18`.
  These failures return before counter advancement or output publication; an
  inner primitive error ignored by its wrapper is not detected by these tests.
- `INC dword ptr [R12 + 0xd0]` at `0x180024ed8` advances the counter modulo
  `2^32` immediately after authentication. No later failure rolls it back.
- After checking `P > 5`, the function copies `P - 5` bytes from reconstructed
  payload `+5` to the pre-GEA output and sets its length. Only then does it
  require context state `5` (`0x180024fdd..0x180025025`). State failure returns
  `0xff8ffffc`, retaining that first output and the advanced counter but leaving
  the second output and length unchanged.
- The check is the literal `CMP [context+4],5` / equality branch at
  `0x180024fdd..0x180024fe5`; all other states reject. It remains applicable
  after direct-restart exhaustion leaves state four with derived keys. A valid
  HMAC at that boundary still increments the receive counter, but does not
  produce post-GEA bytes. `FUN_180007968` returns `-1` before CRC/conversion,
  leaving retained raw status, decoded cache and image event unchanged; the
  reader records that `-1` in its error-pair history.
- In state 5 it copies the same bytes to the post-GEA output, publishes its
  length, and GEA-decrypts the data in place, excluding the last four CRC bytes.
  Success returns zero. Both published lengths include the unchanged CRC.
- The GEA primitive is `FUN_180024790`. Its word count is
  `(message_length - 0x31) >> 1`, so an odd final data length is rounded down to
  complete 16-bit words. The copied trailing byte remains unchanged; the
  primitive neither reads nor writes beyond it.
- `FUN_180007968` then checks a subtype-selected fixed CRC span. For profile 9 /
  sensor type 12, `FUN_180009b88` selects `0x37b0` raw12 bytes, and the later image
  conversion of a valid complete frame writes `108 * 88 * 2 == 0x4a40` output
  bytes consumed by the hardware and engine layers. The count-driven converter
  itself does not impose dimensions; its packing and caller-owned storage are
  documented in [`usbinterface-FUN_180009a64.md`](usbinterface-FUN_180009a64.md).

- CRC is checked on the pre-GEA buffer by `FUN_180009b88` with mode zero and
  the subtype byte at device context `+0x1ec` (9 for this path). It compares
  CRC32-MPEG2 of the fixed `0x37b0` bytes to
  bytes at that offset in order `b0*0x100 + b1 + b2*0x1000000 + b3*0x10000`.
  Failure sets caller status to `0xffffffff` and does not call the downstream
  image conversion `FUN_180009a64`. The authenticated counter remains advanced;
  both temporary buffers are freed. GTLS failure likewise prevents conversion.

## Raw callback and retained image state

`FUN_1800048a0` initializes a local status to zero, calls `FUN_180007968`,
and returns that status. Neither function tests whether a capture request is
active. With a nonnull message, `FUN_180007968` requires decoded storage
`0x180060708`; missing storage returns `-1` without authentication. With storage
and successful temporary allocation, it invokes GTLS regardless of action owner,
remaining frame count or HAL reference validity.
The category-2 branch of `FUN_18001a7ec` requires only the nonnull HAL pointer
`0x180063838` and its nonnull `+0x140` callback after complete protocol admission;
it does not restrict the category-2 command subcode or test capture ownership.

GTLS rejection sets the callback return to `-1` but does not write retained raw
status `0x180060cc0`, decoded storage, or the image event. CRC rejection writes
raw status `-1` and returns it, retaining the decoded storage and image event.
Success converts into the existing decoded storage, calls
`FUN_18000dd84` with selector `8` to signal image availability, and sets raw
status zero. For profile 9, the signal helper selects HAL `0x1800606d0` and
event `HAL+0x288+8*8 == HAL+0x2c8`. Thus an earlier image signal can survive
either failure, whereas CRC failure changes the status read alongside it.
Temporary allocation failure
also returns `-1` without publishing an image. The higher reader counts callback
`-1`, rather than distinguishing authentication, CRC and storage failures.

The next image transaction reaches `FUN_180018dd8` with response selector 8.
That owner resets `HAL+0x2c8` before sending; GTLS sends use selector `0xff`
and do not reset it. `FUN_1800074bc` accepts the completed read only when the
send/wait result is nonzero and retained raw status is not `-1`, then copies
the decoded plane. Consequently retaining an old decoded plane/signal through
an idle rekey does not itself satisfy the next image request: it needs a new
signal after that request's reset. A new valid image replaces the plane and
raw status before its ordinary consumer. The corresponding current reset is
`device/transport.c:goodix_transport_send` clearing response bit 8, and the
status check is in `goodix_transport_complete`; neither reset clears the
retained decoded plane itself.
