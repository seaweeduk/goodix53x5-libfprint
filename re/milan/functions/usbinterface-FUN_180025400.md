# usbinterface.dll FUN_180025400

## Identity and callers

- Address: `0x180025400`; logged name: `gtls_handshake_client_step`.
- Client-role owner reached by profile-9/type-12 initialization and resume through
  `FUN_180007ee0 -> FUN_18000823c -> FUN_180025230 -> FUN_180025ac0`.
- `FUN_180025ac0` selects this owner for context role dword at `+0` equal to one.
  Context state is the dword at `+4`; read/write callbacks are at `+0xf8/+0x100`.

## Client steps and completion admission

States zero and one allocate a 40-byte message, generate 32 random bytes at
context `+8` through `FUN_180027aa0`, and send `[u32 0xff01, u32 40, random]`.
Only a complete 40-byte write advances state to two. State two delegates to
`FUN_180026490`. Successful intermediate steps return `-0x400401`; the outer
`FUN_180025230` loops while the absolute return equals that value.

State four allocates and zeroes twelve bytes, then requests exactly twelve bytes
from the read callback. Zero return is `-0x60000c`, a negative return propagates,
and any positive count other than twelve is `-0x60000b`. With twelve bytes,
the first dword must be `0xff04` (otherwise `-0x100003`) and the second must be
twelve (otherwise `-0x100004`). **The final four bytes are not tested.** On
admission, the unsigned 16-bit values at context `+0xc8/+0xca` initialize the
32-bit client/server counters at `+0xcc/+0xd0`, state becomes five, and the
step returns zero. A failed completion leaves state four and the counters
untouched. States outside zero, one, two and four return `-0x700002`.

The read callback `FUN_18001c2f0` waits on device event `+0x1a0` for 2000 ms,
then reads the retained ring through `FUN_180009424`. A timeout returns `-258`.
It issues one wait per callback invocation. Incomplete protocol cells do not
signal that event: `FUN_18001a7ec` returns before category dispatch until the
declared message has been assembled and its checksum admitted. Intermediate
cells therefore do not restart or extend the pending 2000-ms event wait.
The ring reader `FUN_180007dc4` consumes `min(requested, write_index-read_index)`
bytes, with power-of-two wrap; it does not wait to fill a short read or use the
MCU header to choose a length. `FUN_180009424` resets both indices to zero when
empty. Thus a delivered short completion is a count failure, while extra ring
bytes beyond the requested twelve remain available to a later read. The exact
completion predicates are at `0x1800255a3` (count), `0x1800255ee` (type), and
`0x180025630` (declared length); counter/state publication is
`0x18002566d..0x180025689`.

`DataFromDevice` (`FUN_18001a7ec`) appends every validated category-`0x0d`
payload, excluding the protocol checksum, through `FUN_180009550` at
`0x18001ae60` into device
context `+0x190`'s ring and signals event `+0x1a0`. This branch does not test the
GTLS state, command subcode, pending sender or ACK slot. The category-`0x0b`,
command-zero branch separately updates the acknowledged command's status slot.
An MCU response can therefore be retained while the sender is still waiting
for its ACK. The handshake reads the retained bytes after the send returns;
`FUN_18001c2f0` calls the ring reader at `0x18001c547`.

The actual sender path is `FUN_18001c5a0 -> FUN_180018dd8 -> FUN_180018a8c`.
For GTLS it has response selector `0xff` and no response-event wait. The inner
sender clears only the category-D/command-1 ACK slot, constructs the protocol
checksum and 64-byte transfer cells, and calls `FUN_18001c73c`. After successful
write completion it polls the independent ACK slot for an odd status, with up
to 500 one-millisecond sleeps. `Sleep(1)` allows parser publication while the
send stack is live. Neither sender clears the MCU ring. Write failure or exhausted
ACK polling returns zero to `send_mcu`, which unconditionally sleeps two
milliseconds and maps zero to `-1`. The send/receive split therefore preserves
MCU bytes published during ACK polling.

The two handshake wire messages occupy 44 and 48 meaningful bytes respectively
inside their 64-byte transfer cells: command, little-endian protocol length,
40/44 MCU bytes, and checksum. Bytes after the checksum are outside the declared
protocol message and are not initialized consistently by the native stack-based
framer; they have no MCU or handshake consumer.

State advancement follows successful send completion. A failed first send
leaves freshly initialized state zero, while a failed verification send leaves
state two with derived keys and verified identity. It does not publish state
two or four merely because the corresponding write was submitted.

The ring append owner `FUN_180007e50`, called under the ring critical section
by `FUN_180009550`, appends at most `capacity-(write_index-read_index)` bytes.
It copies across the power-of-two boundary and increments the write index by
the accepted count. It is an ordered byte stream, not a last-packet cache; read
boundaries are chosen by the consumer's requested length.

The GTLS wrapper lock at `0x180084868` and normal sender lock at `0x180063898`
are distinct from the ring descriptor's critical section at `+0x18`. The
parser's ring append requires only that ring lock. Neither the ACK polling
wait nor the GTLS event wait holds the ring lock, so a reader-thread parser
callback can publish while the client owns the wrapper/sender locks.

`create_port_gtls` (`FUN_180020718`) creates a `0x40000`-byte ring backed by
`0x1800a2120` and an initially nonsignalled auto-reset event at device `+0x1a0`.
`FUN_180007ee0` clears ring indices on each attempt but does not reset this
event. A successful wait consumes one signal even if ring bytes remain; repeated
`SetEvent` calls before a wait coalesce. Resource destruction is owned by
`FUN_1800208c4`, which frees the ring descriptor and closes the event.

`restart_gtls_handshake` (`FUN_1800210c0`) invokes the same three-attempt wrapper
directly. These attempts do not perform a sensor/USB reset, rediscover the chip,
reload calibration or reselect/provision the PSK. `deviceInit` may invoke the
wrapper again after its three attempts fail; only identity error `0x700003`
causes the intervening production-item cache clear, without clearing the retained
selected-PSK validity byte.

The direct restart is launched by continuous-read completion
`FUN_180021200`, after `FUN_180017ed8` returns exactly `-1` and error-history
byte `0x1800a2108` is already one. The first such error sets the byte; the
next clears it after considering the restart thread. A null thread handle at
`0x1800a2110` permits creation immediately; an existing handle is waited for
50 ms, and only a result other than timeout permits a replacement thread.
For the reader's nonnull packet pointer, `FUN_180017ed8` forwards the return
from `DataFromDevice`. Its `-1` return comes from an admitted category-2 image
payload whose HAL callback at `+0x140` (`ReadRawData`) returns `-1`. Incomplete
cells and rejected checksums return zero, as do category-D ring publications;
they do not themselves trigger this restart. The history byte is changed only
on `-1`, so an intervening successful parser call does not clear the first
failure. This image-consumer-error restart route is separate from the
initialization and D0-resume worker. See
[reader and FDT ownership](usbinterface-profile9-fdt-event-loop.md).

The second-error history clear at `0x1800213b6` is unconditional after the
admission branch: it also occurs when the existing thread wait returns
`WAIT_TIMEOUT` or `CreateThread` returns null. Timeout retains the existing
handle; every other wait result, including `WAIT_FAILED`, reaches creation
and overwrites the handle with the creation result. This callback does not
close the previous handle. The wait and creation occur while receive-context
critical section `+0x70` is held. The new thread receives that same device
context, calls the wrapper with ring `+0x190` and completion dword `+0x198`,
and only logs a nonzero result; it does not clear its handle on return or
complete the pending capture request. Its own body does not acquire the
receive-context critical section.

Direct restart has no raw-image invalidation postlude. The wrapper resets the
ring indices and completion dword, and the client initializer resets the GTLS
context; neither resets image status `0x180060cc0`, decoded storage pointer
`0x180060708`, image event `HAL+0x2c8`, protocol partial-assembly globals, HAL
reference `+0x248`, image validity, remaining capture count or frame callback.
Incoming ACK/MCU cells can subsequently replace partial assembly by the normal
selector rule. A previously signalled image event remains signalled until its
normal wait/reset owner consumes it. A later successful image replaces decoded
contents and clears raw status under the newly installed keys/counter.

Restart does not take the HAL action lock or clear the FDT worker's event/type
store. Category-3 packets can publish FDT state while the GTLS wrapper holds
its separate critical section and waits for MCU input. They return zero through
the reader and do not change image-error history. The controller may consume
the retained FDT notification after handshake completion, but restart itself
does not impose that ordering on the controller. Individual command sends share
the sender lock; the entire FDT handler and restart are not one atomic action.
See [FDT publication and worker ownership](usbinterface-profile9-fdt-event-loop.md).

## Server identity and key publication

`FUN_180026490` requests exactly 72 bytes. It requires a positive count of 72,
type `0xff02`, and declared total length 72, with the same EOF, negative-read,
count, type and length errors as the completion step. It copies the server
random to context `+0x28` and server identity to `+0x68`, both 32 bytes. The
64-byte concatenation is client random followed by server random.
`FUN_180026930` derives 68 bytes at `+0x88` from that concatenation and the
selected PSK. Key, IV, HMAC key and two initial 16-bit counters occupy
`+0x88` (16), `+0x98` (16), `+0xa8` (32), and `+0xc8/+0xca` respectively.
The derivation is SHA256 P_hash with seed `"master secret" || client_random ||
server_random`, without a string terminator. Starting with `A(1)=HMAC(PSK,seed)`,
it concatenates `HMAC(PSK,A(i)||seed)` and advances `A(i+1)=HMAC(PSK,A(i))`,
copying the first 68 bytes. The call is at `0x180026747`.

`FUN_180027430` computes the 32-byte HMAC identity at `+0x48`, using the derived
HMAC key and the same random concatenation. All 32 bytes must equal the received
identity; mismatch returns `-0x700003` before sending. The successful message is
44 bytes: `[u32 0xff03, u32 44, identity[32], ee ee ee ee]`. Only an exact
44-byte send advances state to four. Keys and both identities remain written on
identity mismatch or send failure, but live counters and ready state are not
published. The next complete initialization clears these retained values.
The comparison result is tested at `0x1800267c4`; the state-four store is at
`0x180026874`.

## Initialization and re-establishment ownership

`FUN_180008398` supplies the selected PSK to `FUN_180025930`, with role one and
the MCU callbacks `FUN_18001c2f0/FUN_18001c5a0`. The initializer clears context
bytes `+4..+0x107`, copies at most 32 PSK bytes to `+0xd4`, and stores PSK length
at `+0xf4` and the callbacks at `+0xf8/+0x100`.

`FUN_180007ee0` holds the GTLS critical section across at most three complete
initialization/handshake attempts. Each clears the caller's completion dword
and ring bytes `+0x0c..+0x13` before reinitialization. It publishes completion
one only for zero handshake return. Each failed attempt sleeps ten milliseconds,
including the last. Its secondary in-progress loop allows five calls, with
five-millisecond sleeps only before another call. Invalid null ring or completion
pointer returns `-0x100001` without attempting initialization.

The first authenticated reply uses the newly derived HMAC key and the
zero-extended server counter at `+0xd0`; no extra initial increment occurs in
the handshake. `FUN_180024940` authenticates before incrementing that dword.
A failed first HMAC leaves it at its initialized value. A successful HMAC
followed by the caller's CRC failure retains the increment. Re-establishment
clears these live counters and installs those derived from the new random pair,
rather than carrying forward the prior reply counter.

See [device initialization and resume](usbinterface-FUN_180020970.md) for the
outer retry and device-context publication, and
[authenticated sensor replies](usbinterface-FUN_180024940.md) for the next
consumer of the admitted key/counter state.
