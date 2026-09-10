# usbinterface.dll Profile-9 FDT Event Loop

## Scope

This note covers sensor type 12, which selects profile 9. It documents the
scheduling and dispatch path around `MilanHV_Down_procedure`, `UP_Occure`, and
`Reverse_Occure` threshold checks.

## Worker Lifetime

`device_enable` (`FUN_18000e9b0`) selects profile 9 and calls
`FUN_1800162ac`. After profile setup it calls `FUN_18000e138`, which creates one
`FUN_18000df20` controller thread suspended and then resumes it.

`FUN_18000df20` is a blocking event worker, not a periodic recalibration task.
Its main loop waits indefinitely on the profile context event at `+0x10`, resets
that event after wake, reads the event type at `+0x08`, dispatches one handler,
and waits again. There is no timeout, elapsed-time comparison, or temperature
read in this loop.

The continuous USB reader is started by `usbEvtDeviceD0Entry` before the
asynchronous `deviceInit` worker is launched. FDT packet reception is therefore
not registered by an individual down/up arm. The parser accepts a packet and
signals the manual-reset worker event independently of HAL wait state `+0x1fc`.
If a packet arrives while the worker is still executing the prior handler or
while its arm command is outstanding, the signal remains set for the next loop
iteration. The parser has one shared event-type/data store rather than a packet
queue, so a later packet can replace an event that the worker has not read. No
software path polls current touch level or synthesizes an event when an arm
completes; dispatch requires an actual parsed packet.

## Profile-9 Event Source

`FUN_18000450c` (`GxFNHV_MilanOpen`) installs `FUN_180005b80`
(`milanget_fdtdata`) at profile callback slot `+0x148`. This is the sensor type
12 MCU FDT-packet parser. Relevant IRQ values are:

| IRQ value | Parser action | Worker event |
| --- | --- | --- |
| `0x0002` | Save down-event touch flag and 12 FDT areas; calculate the FDT-up arm base | `0x0f` |
| `0x0200` | Save up-event data; transform it into the next FDT-down arm base | `0x10` |
| `0x0080` or `0x0082` | Save reverse-event data and update the FDT-down arm base | `0x11` |

After storing the event type at profile context `+0x08`, the parser calls
`SetEvent(profile_context->event_10)`. A manual-FDT response uses a separate
completion event and does not enter this state-machine dispatch.

`FUN_180005b80` receives the payload pointer and command selector. Selector `3`
copies payload bytes `4..27` to the manual result store and signals context
`+0x2d8`; it does not publish a worker event. For asynchronous down/up packets,
the IRQ is the little-endian word at payload `+0`. Down IRQ `2` also latches
the little-endian touch word at `+2` into `0x180060790`; up IRQ `0x200` does
not write that retained touch word. Both copy all 24 raw bytes to `0x180060760`.
Down writes context `+0x200 = 9`; up writes `+0x200 = 10`. Base processing
precedes event-type publication (`15` or `16`), which precedes `SetEvent`.
The input packet is borrowed and these routes do not modify it. The parser
does not consult the hardware arm state at context `+0x1fc`.

The retained touch word is consumed by the down route's `FUN_180004918` call
and diagnostic logging, not by the up handler. A later down packet overwrites
it before its own base calculation. It is not an independently published
up-event field. The down helper requires initialized profile thresholds at
context `+0x318/+0x31a` when the override word at `0x1800606fc` is zero.

For each down, up, or reverse event, the parser copies the 24 FDT sample bytes
after the IRQ/touch header as 12 little-endian words without masking or
rejecting a sample value. Only the up and reverse event routes call
`FUN_180005538`; it transforms a private copy and replaces the down-arm base.
See `usbinterface-FUN_1800048d0.md` for the equivalent transform contract.

The raw event copy remains at `0x180060760` after that transform.
`FUN_1800053f0`, installed at callback slot `+0x70`, returns those exact 24 raw
bytes to the reverse and up handlers. On a reverse IRQ the parser first copies
the prior down-arm base to `0x180060778`; callback `+0x170`
(`FUN_1800053b0`) later returns its 12 high bytes as the prior normalized arm
values. Thus parser ordering preserves both operands while installing the new
hardware down base before worker dispatch. The reverse snapshot also remains
the manual-FDT base until another owner replaces it; see
`usbinterface-FUN_180005420.md`.

## Dispatch

`FUN_18000df20` maps the profile-9 event types through `FUN_18000e1f0`:

| Worker event | Device action | Profile callback |
| --- | --- | --- |
| `0x0f` | `0` | slot `+0x190`: `FUN_180014e10` (`MilanHV_Down_procedure`) |
| `0x10` | `1` | slot `+0x188`: `FUN_180015aa0` (`MilanHV_UP_procedure`) |
| `0x11` | `4` | slot `+0x198`: `FUN_180015a60` (reverse wrapper) |

`FUN_1800162ac` installs those three profile-9 callbacks. The up and reverse
wrappers call `UP_Occure` and `Reverse_Occure` respectively before rearming.

The worker executes the selected handler synchronously. Any
`MilanHV_temperature_event` / `MilanHV_update_allbase` call therefore completes
before control returns to the handler or wrapper. On reverse/up no-refresh paths
the handler calls profile callback `+0x110` with zero before its wrapper calls
arm callback `+0xb0` with one. Refresh branches may return before that handler
postlude, but the up/reverse wrapper still performs the down rearm. The down
handler owns its rearm directly. Refresh state and failure effects are owned by
`usbinterface-FUN_180013da4.md` and `usbinterface-FUN_180015c60.md`.

Each refresh branch attempts full-base acquisition. Complete admission replaces
the retained image and image-valid state. Image-pair or final TX-on FDT
rejection still runs the common FDT postlude, replacing retained FDT-calibration
storage and programmed FDT bases while leaving retained image state unchanged.
On a temperature-event rejection, `+0x232` remains clear and `+0x236` is not
written; any earlier unconsumed marker value is unchanged.

## Hardware Arming And Rearming

`FUN_18000450c` installs `FUN_180005a60` at profile slot `+0xb0`:

- Argument `1` issues the profile-9 FDT-down detect command using the retained
  down-arm base and records wait state `0xf0`.
- Argument `0` issues the FDT-up detect command using the calculated up-arm base
  and records wait state `0xf1`.

The base pointers are `0x180060730` (down) and `0x180060748` (up).
`FUN_180019ec8` (`ChangeMode`) constructs category `3`, command `1` or `2`,
payload `[0x0c, 1, base[24]]` or `[0x0e, 1, base[24]]`. Both lengths are
26 bytes, with checksum selector one, ACK timeout 500 ms, no synchronous
data-response wait, and response-event selector `0xff`. The retained bases
are read-only inputs to this encoding. The protocol-initialized flag at
`0x180063840` must equal one, and `ChangeMode` holds the protocol critical
section while constructing and submitting the command. Other arm arguments
besides exactly zero and one do not issue a command or change wait state.

For either argument, `FUN_180005a60` issues the category-3 mode command first and
writes `+0x1fc` only after command handling returns. `ChangeMode` retries once
on a zero send/ACK result. Independently, if the result returned to
`FUN_180005a60` satisfies `(status & 3) == 3`, the arm wrapper requests mode 4
(configuration download) and invokes the same arm command once more, ignoring
the mode-4 and repeated-arm results. A zero result alone does not trigger this
configuration download. The wrapper returns zero regardless of the final
command result. The FDT-up base consumed by argument zero was generated by
the down-packet parser before worker event `0x0f` was signalled.

The resulting state transitions are:

1. A capture request arms FDT-down detection and leaves the worker asleep.
   Initial action `0x0c` itself does not arm. If its final TX-on FDT validation
   rejected the reference, the first later operation still arms down using the
   first TX-on-derived store and keeps image validity clear.
2. A down IRQ wakes the worker and runs `MilanHV_Down_procedure`.
3. A false/drift down event synchronously attempts full-base acquisition and
   rearms down; no live image is captured. Admission replaces the retained image,
   while validation rejection updates only the FDT stores.
4. A real down event whose live read succeeds completes the sample and switches
   to FDT-up detection. A live read returning `-1` leaves the request and
   callback pending, rearms FDT-down, and returns the worker to its event wait.
5. An up IRQ runs the lift comparison, may attempt full-base acquisition, and
   then rearms FDT-down detection.
6. A reverse IRQ runs its comparison/acquisition path and rearms FDT-down
   detection.

The profile has an asynchronous hardware-event loop but no periodic software
polling. Environmental drift can be noticed while an operation waits because
the sensor remains armed against its programmed FDT base. The software only
evaluates the 12-area thresholds after the sensor emits an FDT event.

Reverse IRQs are part of the refresh contract rather than incidental noise. The
parser updates the hardware's down-arm base on each reverse event, while
`Reverse_Occure` maintains a separate normalized software anchor so multiple
smaller shifts can accumulate into a refresh decision. `UP_Occure` uses that
same anchor. See `usbinterface-FUN_180014480.md` and
`usbinterface-FUN_1800149c4.md`.

## Timer Distinction

Profile-9 `FUN_180014270` (`OnTimerFunc`) is not the environmental-refresh
mechanism. It clears a cached live frame and may rearm FDT-down detection in the
power-button timing path. It does not compare FDT areas or call
`MilanHV_temperature_event`.

## Sleep Command And Independent ACK Reception

Action `0x0e` reaches `Milan_SetMode` (`0x1800059c0`) through HAL slot
`+0x40`. Mode 2 requests category 6, command 0; `ChangeMode`
(`0x180019ec8`) supplies payload `01 00`, checksum selector 1, the caller's
ACK timeout, zero response timeout, and response-event selector `0xff`.
It retries once on a zero `SendDataToDeviceEx` result. Both attempts hold
protocol critical section `0x180063870`. The mode callback ignores the final
transport result, stores HAL `+0x1e0 = 2`, and returns zero. This mode field
records a requested state, not an acknowledged state.

For the profile-9 down/up branches, the first `SendDataToDeviceEx` call is
at `0x18001a428`; `TEST AL,AL` at `0x18001a430` skips retry on **any nonzero
return**, not only status one. Sleep has the same test at `0x18001a0d0`
after its first call at `0x18001a0c8`. Zero takes the shared second call at
`0x18001a56d`; its byte return becomes the final `ChangeMode` result.
The second call is not followed by another retry test. Both calls reuse
the same constructed local payload, checksum selector and timeout values.
There is no intervening sleep, backoff, reset or additional response read.
The outer lock is acquired at `0x180019f97` and released at
`0x18001a5a3`; the per-attempt inner send lock is released between attempts.
For down/up, the caller supplies ACK budget 500; deactivation sleep supplies
200. Neither branch has a synchronous data response. Manual FDT command 3
is different: ACK budget 500, separate caller-supplied data-response budget
and event selector 10; its zero retry includes data-response failure.

`SendDataToDeviceEx` (`0x180018dd8`) takes critical section `0x180063898`
around the send/ACK transaction when protocol-initialized byte
`0x180063840 == 1`; an uninitialized protocol returns zero. Under that lock,
byte `0x18006a110 != 0` instead returns the initial result one without calling
the sender. The send/ACK contract below is the byte-zero route. The capture
cancellation callback does not write this protocol byte.
Selector `0xff` resets no response event and
waits for no separate data response. It invokes `SendDataToDevice`
(`0x180018a8c`), which clears the command's ACK byte at
`0x180068920 + 24 * (category * 8 + command)` **before** USB submission.
Sleep therefore uses ACK slot index `0x30`, address `0x180068da0`.

The single-packet sleep request begins `60 03 00 01 00 46`; `USBSend`
(`0x18001c73c`) submits a 64-byte transfer. On USB-send failure the sender
returns zero without waiting. On success with nonzero timeout, it checks the
slot's low bit, then calls `timeBeginPeriod(1)`, `Sleep(1)`, and
`timeEndPeriod(1)` between checks, for the supplied count of iterations.
It returns the full status byte once bit 0 is set; exhaustion returns zero.
A zero timeout returns one immediately after successful submission. Thus an
odd status byte satisfies this native ACK wait; the helper does not require
the byte to equal one or decode other error bits. The iteration count is not
a strict wall-clock deadline.
`USBSend` maps a negative synchronous WDF write status to `-1`, and a
nonnegative one to zero. The sender maps that `-1` to its byte-zero failure,
so the mode retry covers write failure as well as ACK exhaustion. A received
status with bit 0 clear does not immediately return its other bits: polling
continues and eventually returns zero if no later odd status arrives.
By contrast, status 3 is returned immediately and does not take
`ChangeMode`'s zero retry; the separate FDT arm-wrapper configuration branch
owns that result. Packet checksum rejection in `DataFromDevice` leaves the
ACK slot unsatisfied; it is not a distinct immediate-error return from the
waiting sender. Native command ACK waits have no request-cancellation argument.

`DataFromDevice` (`0x18001a7ec`) dispatches a completed, checksum-accepted
category-`0x0b`, command-0 packet by its payload: byte 0 is the acknowledged
wire command and byte 1 is the status. It writes status to
`0x180068920 + 24 * ((payload[0] >> 4) * 8 + ((payload[0] >> 1) & 7))`
at `0x18001aeac`. It does not call an FDT handler or signal a response event
for this ACK. Category-3 packets instead invoke HAL callback `+0x148`, the
profile-9 FDT parser. Neither path depends on which command's ACK is being
waited for, and the parser takes neither sending critical section. An FDT
packet therefore cannot itself satisfy or clear the sleep ACK slot.

The sleep send/ACK path clears only its command's software ACK status. It
does not drain the receive pipe, reset the parser, consume the worker event,
or reset the sensor. A retry clears the same slot again; there is no sequence
number distinguishing acknowledgments of the two identical attempts.
ACK publication is not restricted to the currently awaited command. An ACK
for a prior different command updates only that prior command's slot, even
while a later command waits. A subsequent send of the same command clears
its slot again. An old ACK arriving after that clear can satisfy the repeated
command, and a newer ACK can arrive after that repeated command has returned;
there is no attempt-generation check or explicit late-ACK discard in the
native parser.
See `usbinterface-FUN_18000e1f0.md` for direct-mode dispatch ownership and
`usbinterface-FUN_18001fb40.md` for capture-request completion ownership.

The continuous read completion callback is `EvtUsbReadPipeReadComplete`
(`0x180021200`). It holds its receive-context critical section at `+0x70`
while calling `FUN_180017ed8` -> `DataFromDevice`. This receive lock is
separate from the HAL action lock and the two protocol send locks. ACK
publication can proceed while a handler is blocked in a command's ACK wait.
`ConfigContReaderForReadEndPoint` (`0x180020058`) installs this completion
callback and `EvtReadFailed` (`0x1800211a0`). The failure callback clears
global active byte `0x1800600a4`, logs the failure and returns byte one; it
does not itself reset the sensor, clear ACK slots, complete the capture
request or issue a drain command. This callback is distinct from a command's
ACK polling timeout.

`EvtUsbReadPipeReadComplete` traverses its receive buffer at **64-byte
boundaries**, invoking the packet parser for each cell. Its loop increments
the 16-bit offset by `0x40` at `0x1800213c6` and compares it with `0x8000`
at `0x1800213ca..0x1800213d2`. The nonzero received-length argument is only
an entry gate; it is not this loop's bound. Two complete protocol messages
can therefore be handled from distinct 64-byte cells of one callback buffer.
Within a cell, `DataFromDevice` consumes only the header-declared message
length; it does not search the remaining padding for another message.
This is cell-framed reception, not packed variable-length messages in the
unused tail of one cell.
The allocation-size producer is device initialization `0x1800232a8`, which
stores word `0x8000` at device context `+0x30`. Reader configuration
`0x180020058` uses that word as its transfer length and requests one pending
read. Thus the fixed traversal bound matches the configured receive-buffer
capacity, not the length of an individual ACK or FDT message.

Category-3 event dispatch does not check whether its arm ACK was received:
an event may be published before that ACK, while the sender continues to
wait on its independent ACK slot. Publication precedes any worker acquisition
of the HAL action lock; receiving the event does not require completing the
arm command first.

Coalescing the worker event does not coalesce parser-side mutations. Every
accepted reverse IRQ first copies the current down-arm base `0x180060730`
to prior-base storage `0x180060778`, then transforms the new raw sample into
`0x180060730`, before publishing event `0x11`. For two reverse packets parsed
before one worker dispatch, the second packet's prior-base snapshot is the
first packet's transformed base, not the base preceding both packets.
The worker may consume only the latest event type/raw sample, but callback
`+0x170` still exposes this updated prior-base snapshot. Similarly, down
packets calculate the up base during parsing rather than when the worker
event is eventually dispatched. An outstanding `ChangeMode` retry still
uses its already-constructed local payload; parser mutation of a retained
base does not rewrite that local command snapshot.

## Request Cancellation And Deactivation

`gfOnCancel` (`0x180020ef0`) only cancels the WDF capture request: it sets
device-context bytes `+0x154 = 1` and `+0x152 = 1`, completes the request with
`0xc0000120`, and clears its `+0xf8` owner. It does not send sleep, stop the
continuous reader, clear the FDT worker event or reset the sensor. Its
construction/request critical sections are device-context `+0xc8/+0x98`,
distinct from the HAL action lock. See `usbinterface-FUN_18001fb40.md`.

`OnActivate` (`0x1800214d0`) owns a separate activation request. On input
byte 0 other than one, its deactivation branch performs this order:

1. Set device-context `+0x154 = 1`, `+0x152 = 0`.
2. Dispatch action `0x13` with dword `0xf2`, storing HAL wait state `+0x1fc`.
3. Store HAL worker event type `+0x08 = 0x15`, then signal event `+0x10`
   when its handle is not `-1`. This uses the same one-slot publication as
   FDT, not a queued stop message.
4. Set its output byte to zero. If the HAL exists, call `FUN_180013454`
   (`gf_broken_check_start_enroll`) and copy HAL `+0x30e` to the output.
   That helper waits up to 1500 ms on HAL `+0x300` only if broken-check byte
   `+0x2e1` is nonzero; it signals that event even after wait failure and
   clears global `0x1800615d4`. This is not a USB receive drain.
5. Dispatch action `0x0e` with dword mode 2 and word timeout 200. Ignore its
   result, then complete the activation request with status zero and
   information length one.

Worker event `0x15` invokes action `0x11` with initial power-control bytes
`00 00`, delay word 500 and command timeout word 200. The action applies its
screen/power-button policy and sends through `FUN_18001afec`. Event dispatch
and direct sleep both hold the HAL action critical section `0x18005e7c8`,
but each action releases it separately. Thus publishing event `0x15` before
the sleep action does not guarantee the power-control command runs first.
`EcControl` (`0x18001afec`) sends category `0x0a`, command 7, payload
`[control[0], control[1], 0]`, checksum selector 1, the supplied ACK timeout,
zero response timeout and selector `0xff`. It makes one attempt, returning
zero on nonzero send/ACK result or `-1` on zero result. Its ACK byte is a
different slot from sleep (index `0x57`, address `0x180069148`). The event
worker only logs an action result of `-1`; it does not complete a capture
request or retry that action.

An up IRQ is still parsed and signals event `0x10` regardless of HAL mode
`+0x1e0`, wait state `+0x1fc`, or the request's stop bytes. If the worker
consumes that event, it invokes action 1, `MilanHV_UP_procedure`
(`0x180015aa0`), which calls `UP_Occure` and then down-arm callback
`+0xb0(1)`. Neither the worker's up dispatch nor this wrapper gates on sleep
mode or request ownership. `UP_Occure` (`0x1800149c4`) also has no such gate.
The deactivation event and a pending FDT event can replace each other's
single event-type slot; native supplies no FIFO or explicit pending-up drain
at this boundary. The HAL action lock serializes command-producing handlers
with sleep, while the independent reader continues receiving both FDT and ACK.

The D0-exit path (`0x180022ff0`) has a distinct reader lifetime: its selected
sleep/power-control action returns **before** WDF table slot `+0x358` stops
the read-pipe target. Request cancellation and `OnActivate` deactivation do
not call that reader-stop operation. D0 entry (`0x180022d70`) starts the
read-pipe target through slot `+0x350` before creating/resuming `deviceInit`.
