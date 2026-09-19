# usbinterface.dll FUN_180018dd8 — synchronous command transactions

## Identity and current mapping

`0x180018dd8` (`SendDataToDeviceEx`) owns the selected response-event reset,
serialized send/ACK, and response wait. The corresponding current owners are
`drivers/goodix53x5/device/transport.c` (`goodix_transport_send`,
`goodix_cmd_set_policy`, `goodix_transport_select_response`, receive completion
and command retry) and the named wrappers in `device/commands.c`.
`goodix_reader_start` / `goodix_rx_cb` own the continuous IN callback;
`goodix_transport_arm_wait` / `goodix_transport_expired` own command budgets;
`goodix_transport_quiesce` joins the reader at hardware boundaries. This note owns
the generic reset/register/OTP/production command contracts; profile-9
mode/manual consumers are also mapped
in `usbinterface-FUN_180005420.md` and the FDT event-loop note.

The physical owner is `FpiDeviceGoodix53x5.reader`; `transport` instead owns one
command or logical event/MCU wait. `goodix_reader_start` posts one 0x8000-byte IN
with no timeout, and `goodix_rx_cb` dispatches its transferred bytes in 64-byte
cells through `goodix_rx_cell` before completing a logical waiter. It does not
inspect the allocation suffix beyond the transferred length. Command expiry,
retry and `goodix_transport_cancel_event` preserve that reader and incomplete
assembly. Successful open/resume and ordinary service/action handoffs likewise
preserve it. Hardware-stop paths join it before `goodix_transport_invalidate`.
A physical read error is retained in the reader and sets `needs_reinit`; these
owners provide no WDF-equivalent pipe-reset/restart operation. Session fault
handling is mapped in `usbinterface-profile9-fdt-event-loop.md`.

## Response ownership and budget origin

Arguments are category, command, payload, 16-bit payload length, checksum flag,
ACK poll budget, unsigned response budget and byte event selector. Selectors
0–6 reset `0x1800688d8[selector]`; selectors 8–10 reset the HAL event at
`HAL+0x288+8*selector`. Reset precedes acquisition of the send critical section
at `0x180063898`; cache bytes are not cleared. Initialized byte
`0x180063840` must equal one. The abort flag is `0x18006a110`.

After `0x180018a8c` returns nonzero, a nonzero response budget starts repeated
`WaitForSingleObject(handle,50)` calls. Each `WAIT_TIMEOUT` adds 50 to an
unsigned counter; another wait occurs only while counter < supplied budget.
There is no packet/fragment-dependent budget renewal. An invalid handle (-1)
or abort during the wait returns zero. Any wait result other than
`WAIT_TIMEOUT` is treated as success. The critical section remains held through
the wait and is released before return; the return is a byte success flag.

The underlying sender `0x180018a8c` clears only the addressed command's ACK
byte at `0x180068920 + 0x18*(category*8+command)` before writing. After the final
write succeeds it polls that byte, accepting any odd value; each unsuccessful
poll decrements the supplied count and sleeps 1 ms. Even nonzero status is not
success. The response wrapper converts a signalled response to byte one,
whereas ACK-only commands can retain the odd ACK status. After the final write
returns zero, a zero ACK budget bypasses polling and returns one to the response
wrapper.

`0x18001a7ec` (`DataFromDevice`) publishes complete checksum-admitted replies
independently of any blocked command and before signalling:

| Incoming family | Shared-cache mutation | Event |
| --- | --- | --- |
| Any category 8 command | Copy payload to `0x1800638d3` | 0 (`0x1800688d8`) |
| A/0, A/1 or A/4 | Copy payload to same prefix | 3 (`0x1800688f0`) |
| A/3 | Copy payload to same prefix | 4 (`0x1800688f8`) |
| Any category E command | Store payload length dword, copy payload at +4 | 6 (`0x180068908`) |

Payload length excludes the envelope checksum. Copies retain every unwritten
suffix; events are separate but the bytes are shared. Thus a reply received
before ACK is retained for the subsequent response wait. A later unrelated
shared-cache writer can replace bytes before the waiting wrapper copies them.
No command/attempt-generation tag protects those bytes. E/1 and E/2 both signal
the production event: response consumption is by event family, not exact wire
command. A late first-attempt response can satisfy a second attempt after its
event reset. Register and OTP consumers copy their requested size without
checking the received payload length. Framing/checksum rejection does not
signal any of these events.

Category B/command 0 ACK publication writes payload byte one into the slot
selected by payload byte zero: index `(ack_command>>4)*8 +
((ack_command>>1)&7)`. It does not signal a response event or reject an ACK
because a different command is blocked. Late ACKs therefore replace their own
slot without completing another command's ACK or data wait.

`0x18001b4a0` creates selectors 0–6 with
`CreateEventW(NULL,FALSE,FALSE,NULL)`: they are initially clear, auto-reset
events. Multiple publications coalesce; successful wait consumes the signal,
not the cached bytes. Protocol initialization publishes the initialized byte
only after creating all seven handles. Failure closes created handles and
replaces those entries with -1.

## Independent USB Receive Boundary

`ConfigContReaderForReadEndPoint` (`0x180020058`) zeroes a 0x48-byte WDF
continuous-reader configuration, sets its transfer length from device word
`+0x30` (`0x8000` from device construction), sets one pending read, and installs
completion `0x180021200` and failure `0x1800211a0`. The decisive stores are
`0x1800200c9..0x1800200f5`. It does not select a transfer size from a command,
image header, declared remaining length or sender deadline. A completed image
transfer shorter than 0x8000 is therefore not the posted request size.

Read completion uses received length only as a nonzero gate and traverses the
full 0x8000-byte allocation in 64-byte cells, provided its first byte is nonzero.
The loop does not bound traversal by actual transferred bytes; see the
[FDT event-loop note](usbinterface-profile9-fdt-event-loop.md) for its exact
instruction boundaries. First cells carry
three header bytes and up to 61 declared bytes; continuations carry one selector
and up to 63 declared bytes. For declared size `n` (including checksum), native
`DataFromDevice` computes `ceil((n+2)/63)` cells. Single-cell `n < 62` copies
only `n` bytes, and the final continuation copies only the remaining declared
bytes. Padding is not another packed message. A different selector abandons
an active partial message; any even first-cell selector starts a new message;
an orphan odd continuation returns zero. These rules apply equally within one
completed USB buffer and across read completions.

Checksum rejection returns zero without ACK, response-event or FDT publication.
ACKs replace their independent status slots; an unrelated ACK is not a command
failure. Complete packets after an ACK or data reply in the same read buffer
still pass through the callback's cell loop. This is not a callback that returns
after finding one sender's reply.

ACK/response expiry belongs to the blocked sender, not this posted read. The
sender neither cancels the continuous read nor clears partial assembly on
expiry/retry. USB read failure instead reaches `0x1800211a0`, which clears the
initialization-active byte and returns TRUE for framework pipe recovery; it
does not parse a partial failed-transfer buffer. D0 exit stops the read target.
Capture cancellation alone does not stop that target. Consequently the DLL
provides no per-64-byte completion guarantee for a truncated image interrupted
by one padded cell: parser visibility begins at completed USB-read callbacks,
not at each protocol cell received by the USB stack.

## Reset and chip-register consumers

`0x18001b6c8` (`Reset`) sends selector `0xa2` once with ACK budget 500.
For reset type zero its payload is `{output ? 5 : 1,20}`. A nonnull IRQ output
selects event 3 and response budget 1000; a null output selects -1 and zero
response budget. Success returns zero; failure returns -1. Type-zero success
with output assembles `cache[0] + (cache[1]<<8) + (cache[2]<<16)` from
`0x1800638d3` into the caller's integer. The transport maps the two forms using
the request's `expect_data` flag; the IRQ form's completed reply view selects
the shared system-response cache. Current `goodix_cmd_reset_sensor` maps both
type-zero forms through its `request_irq` argument.

Type-one reset, used after GFESD exhaustion, sends `{2,50}`. With null IRQ
output it has the same one-attempt ACK-500/no-response policy and zero/-1
result. Neither reset form itself sets a deferred reinitialization flag.
Firmware-update owner `0x180019b68` also calls type-one/null-output reset after
its firmware-check transaction succeeds and shared cache byte zero is nonzero;
it ignores the reset result and retains the check transaction's success result.
That is a firmware-update postlude, separate from profile-9 chip discovery.

`0x18001a604` sends selector `0x82` with five payload bytes
`{0,addr_lo,addr_hi,size_lo,size_hi}`. Each attempt uses ACK 500, the caller's
16-bit response budget and event 0. A zero transaction result repeats the same
payload once. Only a successful attempt copies the requested number of bytes
from `0x1800638d3` into caller storage; both failures return -1 without copying.
`goodix_cmd_read_chip_id` maps address zero, size four.

`0x180017ef8` supplies response budget 200 for that chip read. On successful
read it assembles `(b0<<8) + (b3<<16) + (b2<<24) + b1` and writes HAL `+0x24`.
The recognized `chip>>8 == 0x220c` branch writes HAL `+0x28 = 9` and returns
zero. See `../CHIP-ID-PROFILE-MAPPING.md` for family identification ownership.
Failed reads and unrecognized IDs call `Reset(0,&irq)`, ignore its result,
then `Sleep(100)`, including the last failed cycle. Instructions
`0x180017f98..0x180017fa0` increment an initially zero byte and compare its old
value with five: six read cycles, each containing up to two transactions.
Exhaustion returns -1. The current chip-result consumer is the open state
machine in `device/session.c`.

`device_enable` (`0x18000e9b0`) ignores the initial `Reset(0,NULL)` result,
then sleeps 10 ms before chip identification. Identification failure prevents
profile initialization and publishes profile sentinel 13 in the global and
device `+0x5c` fields. This outer owner, rather than the transaction helper,
owns the failed enable result.

Register adapter `0x18001809c(address,size,out)` supplies response budget 200;
`0x1800180b0(address,out)` supplies size two and response budget 500. Both
preserve the underlying register-read result in EAX. Profile-9 callback
`0x180005200` (`Milan_GetDacVal`), installed at HAL `+0xe8` by `0x18000450c`,
first calls mode 7 with budget 200, ignoring its result, then reads register
`0x220` into HAL `+0x22c`, byte-swaps that word, updates tcode-dependent
`+0x22e` (mode 0 →40; mode 1 →26), and returns the register-read result.
Byte swap and delta update also occur after read failure. See
`usbinterface-FUN_180005200.md` for the DAC owner. The generic two-byte register
OCALL is `0x18000dce0`; the test adapter `0x18001f5f8` uses the sized/200 form.

`0x18000d660` (`GFESD_procedure`) is a further profile-9 reset/register
consumer. It requires a nonnull, enabled (`+0x204 != 0`) HAL and uses mask
`0x410` for profile 9. With `(irq_word_at_+0x282 & 0x410) != 0`, each of up to
ten iterations sends IRQ-returning reset, sleeps 6 ms, and reads chip register
zero/four bytes/200. A successful matching chip (`+0x27c`) reaches repair;
a mismatched successful read sleeps 200 ms, while failed read sends another
IRQ reset then sleeps 200 ms. With the mask clear, each iteration first
requires successful IRQ reset and `irq>>8 == 0x410`, then resets again,
sleeps 6 ms and reads/compares chip ID (this branch does not check read status).
Failure to reach matching chip sleeps 200 ms. Exhaustion sends type-one reset
with null output; it does not force the function return to failure. Matching
chip invokes HAL `+0x40` with local value 4, calls `0x18000d24c`, then calls
HAL `+0xb0` with mode 1. Both recovery branches set HAL dword `+0x200 = 0x10`; the retained
read/reset result is returned. Current platform recovery is owned by
`device/session.c` and `device/scan.c`; no separate GFESD command wrapper is
present in `device/commands.c`.

`device_action` (`0x18000e1f0`) action 2 dispatches GFESD under the enabled-HAL
critical section and returns its result. Action 6 dispatches type-zero reset
with the caller's optional IRQ destination before the HAL-null/enabled checks.
The register OCALL's profile-9 caller `0x1800077e4` is the navigation-data
callback: after CRC success, only global `0x180060798 == 1` reads registers
`0x72`/`0x74` into initially zero words and combines them for navigation decode;
read statuses are ignored. This is distinct from fingerprint image acquisition.

The interrupt worker `0x18000df20` maps HAL event code `+8 == 0x12` to action 2;
it only logs a -1 action result before waiting again. The installed profile-9
FDT parser `0x180005b80` publishes event codes `0x0f`, `0x10`, `0x11` for
down/up/reverse and `0x14` for its unrecognized-IRQ branch; it does not itself
publish `0x12`. The explicit GFESD dispatch must not be conflated with these
ordinary FDT events.

The HAL `+0xe8` DAC callback is invoked by `0x18001f520`
(`Test_OpenShortUpdateDAC`) when its byte argument is nonzero; it then invokes
HAL `+0xf0` with 1. Zero skips the register-read callback and invokes `+0xf0`
with 2. These factory/test entry points are distinct from the open chip read.

Factory `0x18001f674` (`Test_Reset`) accepts only byte argument one, zeroes a
local IRQ integer, calls action 6 with its address, ignores command status,
optionally copies `uint16(irq>>8)` to the caller, sleeps 6 ms and returns zero.
Other byte arguments return -1 without a command. `0x18001f764`
(`Test_SetIdleMode`) calls action 6 with null output, ignores its result,
sleeps 6 ms and returns zero. These have no current factory-API counterparts.

## OTP and production transactions

`0x18001b2e0` (`GetOTP`) rejects a null destination. It sends selector `0xa6`,
payload `{0,0}`, ACK 500, caller-supplied 16-bit response budget, event 4.
Zero transaction result repeats once. Success copies the caller's 16-bit
requested size from `0x1800638d3` and returns zero; exhaustion returns -1
without copying. Current counterpart: `goodix_cmd_read_otp` and the open OTP
consumer in `device/session.c`.

The profile-9 `0x18000450c` initializer installs the OTP thunk at HAL `+0x138`.
Sensor checker `0x180004a40` calls it directly at `0x180004add` with size 32
and response budget 200. Exhausted read returns before OTP verification or
calibration publication; see `usbinterface-FUN_180004a40.md`.

`0x18001b9c4` (`production_read`) takes type, destination, unsigned capacity,
and output-length pointer. Null output pointers return `-0x100001`. Nonempty
`0xb001` and `0xb003` caches satisfy a sufficient-capacity call without wire
traffic. Their lengths are `0x18007a118`/`0x18007a11c`, their byte stores
`0x18007a120`/`0x18007f120`. Any unsuccessful cache lookup falls through to
selector `0xe4`, four-byte little-endian type, ACK 500, response 1000, event 6,
with one repeat on zero transaction result. Both transport failures return
`-0x200003`. The common response cache begins with a 32-bit length at
`0x1800638d3`, then status at `+4` and data at `+5`.
Length <2 or length >capacity returns `-0x100006`; nonzero status returns
`-0x200004`. Status zero copies length-1 bytes and writes output length.
Successful type-b001/b003 reads update their persistent caches only when the
output length is at most `0x5000`. Cache-update failure does not revoke success.

`0x18001c084` (`production_write`) rejects null data with `-0x100001`, then
zeros both `0x5000`-byte production stores and both lengths **before** sending.
It transmits the caller's existing payload, length narrowed to 16 bits, using
selector `0xe2`, ACK 500, response 1000, event 6. Zero transaction result
repeats once. A nonzero transaction result and status byte `0x1800638d7 == 0`
return zero; exhausted transport or nonzero status returns `-0x200004`.
It does not restore caches on failure or construct a type/length prefix.
Current counterparts are `goodix_cmd_production_read/write`, their reply
parsers, and selected-PSK consumers in `device/session.c`.

Production adapters `0x180008774`/`0x180008870` select b001/b003 respectively,
take input capacity through the in/out length pointer, and clear that length on
any read failure. Their caller is `0x180008b54`. Write adapters
`0x18000896c`/`0x180008a60` select b001/b002 respectively, forward an already
encoded payload and propagate the result to `0x180009054`.

`0x180008b54` allocates separate 0x800-byte b001, b003 and key buffers. It
reads b001 then b003, requires each returned TLV length at least nine, checks
the exact types and b003 declared value length 32, hashes 32 bytes copied from
b001 value `+8`, and copies the key to its caller only on full hash equality.
Any failed read/check leaves caller key unpublished; all allocated temporaries
are freed. `0x180009054` constructs b002 and b001 TLVs, writes b002 before b001,
then invokes that check and compares all 32 checked/generated bytes before
publishing. Failed write or verification prevents later steps. The surrounding
cold-key attempts and retained-key/cache lifetime belong to
`usbinterface-FUN_180020970.md`, “Cold GTLS PSK Acquisition”; the current
selected-key/hash consumer remains the corresponding `device/session.c` states.

Status rejection in these wrappers occurs after the transaction and does not
cause a wire retry. Transaction retries reset only the response event, preserve
cached bytes, and reuse the same payload with fresh ACK/response budgets.
