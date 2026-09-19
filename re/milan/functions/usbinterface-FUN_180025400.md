# usbinterface.dll FUN_180025400

## Identity and callers

- Address: `0x180025400`; logged name: `gtls_handshake_client_step`.
- Client-role owner reached by profile-9/type-12 initialization and resume through
  `FUN_180007ee0 -> FUN_18000823c -> FUN_180025230 -> FUN_180025ac0`.
- `FUN_180025ac0` selects this owner for context role dword at `+0` equal to one.
  Context state is the dword at `+4`; read/write callbacks are at `+0xf8/+0x100`.

### Current source mapping

- `usbinterface.dll!0x180025400` and identity helper `0x180026490` map to
  `drivers/goodix53x5/device/session.c:goodix_gtls_ssm_handler`; key derivation
  and identity calculation map to `device/crypto.c:goodix_crypto_gtls_derive_keys`
  and `goodix_crypto_gtls_verify_identity`.
- `usbinterface.dll!0x180007ee0` maps to `device/session.c:goodix_gtls_retry_handler`
  and `goodix_gtls_attempt_done`; restart entry `0x1800210c0` maps to
  `goodix_start_gtls_restart` and `goodix_gtls_restart_done`.
- `usbinterface.dll!0x180025930` maps to
  `device/crypto.c:goodix_crypto_gtls_init`, called from
  `device/session.c:goodix_gtls_ssm_handler` before each client hello.
- `usbinterface.dll!0x180021200`'s image-error history maps to
  `device/transport.c:goodix_rx_cell`, called by `goodix_rx_cb` for each
  transferred cell. The Linux pending restart is consumed by
  `goodix_transport_wait_event` / `goodix_transport_complete` and
  `device/scan.c:goodix_scan_event_done`, with continuation through
  `goodix_scan_gtls_restarted`. Both foreground and request-independent service
  modes of the coordinator use this handoff; restart shares their serialized
  command owner while the physical reader remains independent.
- Idle restart exhaustion in `goodix_gtls_retry_handler` fails the service
  coordinator. `goodix_session_service_done` latches its error;
  `goodix_session_settled` / `goodix_session_fault_joined` join and invalidate
  transport before reporting `fpi_device_session_error`. Subsequent foreground
  admission is blocked until close/open. Foreground ordinary restart exhaustion
  instead completes the restart coordinator without an error, retaining the
  failed attempt's GTLS state. Native restart itself only logs final failure,
  as described below.

## Client steps and completion admission

States zero and one allocate a 40-byte message, generate 32 random bytes at
context `+8` through `FUN_180027aa0`, and send `[u32 0xff01, u32 40, random]`.
Only a complete 40-byte write advances state to two. State two delegates to
`FUN_180026490`. Successful intermediate steps return `-0x400401`; the outer
`FUN_180025230` loops while the absolute return equals that value.

The hello random producer `FUN_180027aa0` clears its returned length, acquires
a Windows crypto provider with `CryptAcquireContextW(PROV_RSA_FULL,
CRYPT_VERIFYCONTEXT)`, and calls `CryptGenRandom` for 32 bytes. Provider or random
failure returns `-60`; an acquired provider is released on either random outcome.
Only success publishes the requested length and returns zero. At
`0x18002579a..0x1800257a3` the hello owner tests that return before copying or
sending any random bytes. Failure frees the 40-byte message, propagates the error,
and leaves the entry state unchanged. Successful random generation followed by
short/failed send likewise does not advance state. Hello-message allocation
failure returns `-0x100005` before random generation or send. The corresponding
Linux producer/caller boundary is `device/session.c:goodix_gtls_ssm_handler`
(`GOODIX_GTLS_HELLO`) and `RAND_bytes`.

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
or reload calibration. With selected-PSK validity already set, they do not
reselect/provision the PSK. `deviceInit` may invoke the
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
they do not themselves trigger this restart. Within read completion, the history
byte is changed only on `-1`, so a successful parser call does not clear the first
failure. This image-consumer-error restart route is separate from the
initialization and D0-resume worker. See
[reader and FDT ownership](usbinterface-profile9-fdt-event-loop.md).

Restart admission in `FUN_180021200` has no active-action, capture-request,
frame-count, HAL-validity or FDT-wait predicate. With a nonnull receive context
and delivered buffer, a second parser `-1` considers thread creation even when
there is no active scan. The restart entry likewise invokes the handshake
wrapper without testing an action owner. The native thread can therefore
replace keys before any subsequent application action; it does not wait for a
new action to consume a pending flag.

Device construction `FUN_1800232a8` clears history at `0x1800235be` after
successful port construction. The reader's `0x1800213b6/0x1800213bf` stores
then implement the error-pair lifetime. A completed restart does not reset
history accumulated during that restart; success alone is not a history reset.

Capture cancellation `FUN_180020ef0` writes device bytes `+0x154/+0x152`,
completes and clears the request at `+0xf8`, and leaves the image-error history,
restart-thread handle, MCU ring/event and GTLS context untouched. Neither
`FUN_180021200` nor `FUN_1800210c0` tests those request fields. Cancellation
therefore does not suppress an already admitted or later reader-driven restart.
Request cancellation and hardware/reader teardown are separate boundaries;
see [request and power lifetime](usbinterface-profile9-fdt-event-loop.md).

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

The caller tests both derivation and identity-HMAC returns before comparison or
verification send. `026930` rejects missing SHA256 metadata (`-0x400304`), seed
workspace overflow (`digest_size + 13 + random_length > 128`), and failed HMAC
context setup `028100` (`0x180026a1a..0x180026a21`). After successful setup it
does **not** test the returns from HMAC start `027ec0`, update `0280a0`, finish
`027db0`, or reset `027e60`: the sequence at `0x180026a32..0x180026ac1` continues
through output copies, cleanup and a zero return. Thus caller-level failure
propagation is not a claim that every primitive failure is detected.

Identity HMAC `027430 -> 027c30` checks the digest-context allocation and the
two-block HMAC-pad allocation; either failure returns `-0x5180`, releasing an
already allocated digest context when pad allocation fails. Once allocated,
`027c30` does not test the digest callback results before its zero return. The
Linux HMAC/P_hash counterpart uses fixed SHA256 GLib `GHmac`, while its AES and
entropy operations use OpenSSL; these have different allocation/error APIs.

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

There is no prior-state admission test in this initializer. The stores at
`0x1800259f2..0x180025a25` clear the old state, randoms, identities, derived
keys and counters before installing the role, PSK and callbacks. Consequently
the complete `007ee0 -> 008398 -> 025930` wrapper starts `025400` in state zero
even if the previous client was in state five or stopped partway through a
handshake. Calling the step alone in state five instead returns `-0x700002`;
it is not the re-establishment entry. The wrapper sends a new `0xff01` without
a preceding sensor reset, firmware query or configuration upload. This is a
host-side sequence contract; the MCU firmware's handling of that new hello is
not implemented in this DLL.

The PSK getter `FUN_18000979c` checks selected-valid byte `0x1800607bc`.
When nonzero it copies the retained 32 bytes at `0x180060c98` and returns
length 32, without a production read or write. When zero it first calls
`FUN_180008560` and propagates initialization failure. `FUN_180008398` requests
the PSK into a 1024-byte local buffer before clearing the GTLS context. Thus
the established-session restart boundary retains the selected PSK and its
validity independently of every attempt's session-key/context reset. The
current counterpart at this boundary is `device/session.c:goodix_gtls_ssm_handler`
passing retained `self->psk` to `goodix_crypto_gtls_init`; initial selection is
owned by `goodix_load_psk`, called by `GOODIX_OPEN_PARSE_OTP` on each cold open
or reinitialization. In-session restart uses the existing selected key without
repeating chip/OTP/configuration acquisition. Joined close clears selected-key
ownership. These source owners have no cached-startup or warm checkpoint path;
the native initialized worker's retained-HAL failure postlude remains separate.

`FUN_180007ee0` holds the GTLS critical section across at most three complete
initialization/handshake attempts. Each clears the caller's completion dword
and ring bytes `+0x0c..+0x13` before reinitialization. It publishes completion
one only for zero handshake return. Each failed attempt sleeps ten milliseconds,
including the last. Its secondary in-progress loop allows five calls, with
five-millisecond sleeps only before another call. Invalid null ring or completion
pointer returns `-0x100001` without attempting initialization.

Send and read failures belong to this wrapper's attempt, not to a deferred
device-initialization request. The return from each new handshake replaces the
previous attempt's return in `EBX` at `0x180008068`; zero reaches the completion
store at `0x18000816d` and exits without retaining the earlier error. Nonzero
returns take the ten-millisecond delay and retry through attempt three. The
restart entry tests only the wrapper's final return at `0x180021115`; failure
adds a log call, and both branches share the same return tail. Neither branch
queues a device initialization or changes the HAL reference/capture owner.

The first authenticated reply uses the newly derived HMAC key and the
zero-extended server counter at `+0xd0`; no extra initial increment occurs in
the handshake. `FUN_180024940` authenticates before incrementing that dword.
A failed first HMAC leaves it at its initialized value. A successful HMAC
followed by the caller's CRC failure retains the increment. Re-establishment
clears these live counters and installs those derived from the new random pair,
rather than carrying forward the prior reply counter.

After ordinary three-attempt exhaustion, the final failed attempt's context
remains installed. In particular, a successful verification send followed by
completion-read timeout leaves state four, both derived initial 16-bit counters,
the new randoms/identities/keys, and live counters zero in the absence of
interleaved authenticated images. The wrapper and restart entry neither restore
the old session nor promote state four to five. Initial counter zero is a valid
derivation result; neither derivation nor completion admission excludes it.
Thus a correctly authenticated image can reach the state-four rejection in
`FUN_180024940`; HMAC validity does not prove that completion was admitted.
The raw consumer's state check and surviving counter/cache/event effects are
owned by [authenticated sensor replies](usbinterface-FUN_180024940.md).

See [device initialization and resume](usbinterface-FUN_180020970.md) for the
outer retry and device-context publication, and
[authenticated sensor replies](usbinterface-FUN_180024940.md) for the next
consumer of the admitted key/counter state.
