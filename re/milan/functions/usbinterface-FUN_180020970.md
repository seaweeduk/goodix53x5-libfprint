# usbinterface.dll FUN_180020970

## Identity

- Address: `0x180020970`
- Logged name: `deviceInit`
- Role: asynchronous initialization worker launched from UMDF D0 entry.

For sensor type 12, `FUN_18000e9b0` selects profile 9; action 9 and action 12
dispatch its sensor-check and all-base callbacks. Both full-init and resume
session routes call the same `FUN_180007ee0` wrapper and common GTLS client
owners.

## Full-Initialization Firmware Probe

Before `device_enable`, the uninitialized branch runs a maximum of five probe
iterations. Each iteration calls `ChangeMode(0, 0, 0, NULL, 500)` through
`0x180017ec0`, ignores that result, and then calls version wrapper `0x180017fe8`
with destination device context `+0x111` and response budget 2000. The category-0
command is inside the loop, not a one-time prelude.

Version wrapper `0x180017fe8` rejects a null destination with `0xffffffff`;
otherwise it forwards to `GetEvkVesion` (`0x18001b15c`). The getter sends
category `0x0a`, command 4, payload `00 00`, checksum enabled, ACK budget 500,
response budget `(uint16_t) supplied_timeout`, and response selector 3
(event handle `0x1800688f0`). It makes a second identical transaction only
when the first sender result is zero. Each attempt resets response readiness
before sending and uses the ordinary odd-ACK predicate. Response waiting tests
event completion, not a version status byte or string predicate.

For this query `DataFromDevice` (`0x18001a7ec`) copies the received payload,
excluding its checksum, to shared cache `0x1800638d3` and signals selector 3.
The parser does not clear the remainder of that cache; the getter does not
clear it before sending. On successful sender completion the getter copies
exactly 64 cache bytes to its caller and returns zero. It neither returns the
received string length nor appends a terminator or checks a firmware prefix.
On two zero sender results it returns `0xffffffff` and leaves the caller's
64-byte destination untouched. A zero response timeout disables event waiting
in the generic sender; this is distinct from a response packet with no version
characters and is not the timeout used by `deviceInit`.

Selector 3 and its cache are not private to the version command.
`DataFromDevice` routes category-`0x0a` commands **0, 1 and 4** through the
same payload-copy and `SetEvent(0x1800688f0)` branch. There is no active-command
predicate. A response for command 0 or 1 can therefore satisfy a pending
version wait and supply that getter's 64-byte cache projection, including when
received before the version ACK. Category-`0x0a` command 3 also replaces the
cache prefix but signals selector 4 instead. Category 8 copies its payload to
the same cache and signals selector 0. These non-version writes affect retained
suffix bytes observable in a later short version response without themselves
signalling version readiness.

The same parser's category-`0x0e` branch writes a dword payload length at cache
offset zero and copies payload at offset four, signalling selector 6;
category-`0x0f` replaces only cache byte zero and signals selector 2. Those
stores also belong to the shared-cache lifetime. The version getter observes
only its first 64 bytes; it does not expose the full backing allocation or
require a separate version-only cache.

Only getter result `-1` increments the outer iteration count and invokes
`Sleep(100)`. The sleep precedes the count comparison, so the fifth failed
iteration also sleeps 100 ms. Success exits the loop immediately, without this
delay. Five failed iterations log version failure and then continue to the
existing initialization predicates and `device_enable`; exhaustion itself is
not a USB bus-reset command or an early thread return. The optional firmware
update check is entered only after a non-`-1` getter result when device-context
byte `+0x153 == 1`.

That byte is copied from configuration `+0x431` by device-object initializer
`FUN_1800232a8:0x1800234ac..0x1800234b2`; its compiled configuration byte
at `0x18005e771` is one and configuration loading can replace it. The call
at `0x180020abf` is wrapper `FUN_1800180c4`, forwarding to
`FUN_18001b7f8` (`UpdateFirmware`). This occurs before chip/profile discovery.
The updater parses the firmware platform, selects an embedded HT/ST image,
and calls `FUN_1800194ac`. Its APP/version-equality path can return without
wire work; other admitted paths request firmware-mode transition or download.
The initialization worker ignores the updater return. Neither the profile-9
algorithm selection nor the final version-query return is its enable gate.
This firmware-maintenance dispatch has no counterpart in the Linux open SSM.

## Startup Reset And Chip Recovery

`device_enable` (`0x18000e9b0`) calls `Reset(0,NULL)` at `0x18000ea38`
without testing its result, then unconditionally sleeps 10 ms. The reset
wrapper `0x18001b6c8` sends `a2` with payload `01 14`, one ACK-500 transaction
and no response wait. The next call is chip getter `0x180017ef8`.

The getter reads register zero, size four, response budget 200 through
`0x18001a604`; that wrapper owns up to two transactions per getter cycle.
Only successful reads decode `(b2<<24) | (b3<<16) | (b0<<8) | b1` and store
the chip at context `+0x24`. The unsigned `chip>>8` comparisons at
`0x180017f61..0x180017f7f` accept exactly `0x2202`, `0x2207`, `0x2208`,
and `0x220c`; the last selects profile 9/type 12. A recognized different
family is a successful native identification, distinct from an unknown chip
or failed read. Linux's profile-9 support gate follows identification and does
not send the other recognized families through chip-getter recovery. Identification mapping is
owned by [the chip/profile map](../CHIP-ID-PROFILE-MAPPING.md).

Each failed read or unknown family calls `Reset(0,&irq)` at `0x180017f88`,
ignores its result and IRQ value, then sleeps 100 ms. This form sends `a2`
payload `05 14`, one ACK-500/response-1000 transaction using shared event 3.
Successful reset copies the low three shared-cache bytes to a 24-bit
little-endian IRQ integer; failed reset leaves the destination untouched.
The getter increments its byte counter after the sleep and compares the old
value against five (`0x180017f98..0x180017fa0`), allowing six read cycles.
The sixth failed cycle still performs reset and sleep before returning -1.
Successful identification returns immediately without the recovery reset/delay.
The getter does not clear chip/profile storage on failed reads; a successful
unknown-chip read still replaces the chip field. The enclosing enable failure
publishes profile sentinel 13 instead of initializing a profile.

The transport/event contracts belong to
[the synchronous transaction owner](usbinterface-FUN_180018dd8.md).
The corresponding Linux owners are
`device/commands.c:goodix_cmd_reset_sensor` (both type-zero payloads) and
`goodix_cmd_read_chip_id` (register request), with
`device/session.c:goodix_chip_ssm_handler` / `goodix_chip_result` owning the
initial reset/delay, six-cycle recovery, chip publication and support gate.
`goodix_cmd_parse_chip_id_reply` consumes the transport's selected shared-cache
view, with the register slot's received length supplying Linux's four-byte check.
The recovery consumer waits for the reset response event but discards IRQ
bytes, matching the native getter's unused local output. Ordinary transaction
failure is distinct from Linux cancellation/removal and malformed-input errors.

## Cold GTLS PSK Acquisition

After action 9 completes sensor checking, the full-initialization route calls
`FUN_180007ee0`. Its `FUN_180008398` initialization calls
`FUN_18000979c` (`production_get_psk`). When process-global PSK-valid byte
`0x1800607bc` is zero, that owner calls `FUN_180008560`, which first tries
`FUN_180008b54` (`production_psk_check`) up to three times.

A successful first check reads production item `0xb001` through
`FUN_180008774`, then item `0xb003` through `FUN_180008870`. Both reach
`FUN_18001b9c4` (`production_read`). An empty process-global item cache causes
a category-`0x0e`, command-2 transaction carrying the little-endian item type,
with ACK budget 500, response budget 1000 and response selector 6. A zero sender
result causes one identical retry. The check requires a type-`0xb001` record
and a type-`0xb003`, 32-byte hash record. `FUN_180008b54` copies 32 bytes from
the first record's value at `+8` directly into its temporary key, calls
`FUN_180027600` to hash that key, and compares all 32 hash bytes. This DLL
does not invoke a host unsealing transformation between that copy and hashing.
Success stores the PSK in process-global storage and sets `0x1800607bc` to one.

`FUN_180008560` clears the PSK-valid byte and all 32 retained key bytes on
entry. It permits three `production_psk_check` calls, then, only if all fail,
three `FUN_180009054` generation/write/check attempts. The latter generates
a 32-byte key, white-box encodes it for item `0xb002`, writes that item, writes
the raw-key type-`0xb001` record, checks through `FUN_180008b54`, and compares
the checked key against the generated key. Either successful route publishes
the retained key and validity byte; final failure leaves validity clear.

The selected-key counterpart is `goodix_load_psk` and the
`GOODIX_OPEN_READ_PSK_HASH` through `GOODIX_OPEN_GTLS_CLIENT_HELLO` states in
`drivers/goodix53x5/device/session.c`. Linux loads an imported key or selects
the all-zero key, reads `0xb003`, refuses an imported-key mismatch, and otherwise
writes its default `0xb002` white box and verifies the hash. This maps the
selected-key/hash handoff; it is not the native key-generation or item-cache
implementation.

When `0x1800607bc` is already one, `production_get_psk` copies the retained
32-byte process-global PSK without calling `production_initialize_PSK` or
issuing either production read. `FUN_18001b94c` clears the separate `0xb001`
and `0xb003` item caches after the selected handshake error, but does not clear
the PSK-valid byte. Thus the production reads belong to cold PSK initialization,
not every later handshake in the same DLL lifetime.

The item-cache bytes never pass to GTLS initialization directly:
`FUN_180008398` receives the selected 32-byte key from `FUN_18000979c` and
passes it to `FUN_180025930`. The valid-key fast path at
`0x1800097ab..0x1800097b8` bypasses all item reads even after item-cache clear.
Linux's retry group likewise reuses its selected `self->psk`. Every new open
reloads the local selected key; only its cold route validates `0xb003` again.

The successful client handshake sends type `0xff01`, receives type `0xff02`,
sends type `0xff03`, and receives type `0xff04`. Both sends use
`FUN_18001c5a0`, which issues category `0x0d`, command 1 with ACK budget 500,
no response wait, then unconditionally calls `Sleep(2)`. The nominal two-send
client sequence therefore contains two fixed 2-ms post-send sleeps. The
`Sleep(5)` in `FUN_180007ee0` is conditional on its handshake wrapper returning
the in-progress error. Its `Sleep(10)` follows each failed initialization or
handshake attempt, including the third/final attempt before returning failure.

The client step's completion admission requires an actual twelve-byte read,
type `0xff04`, and a declared total length of twelve; it does not inspect the
last four bytes as a result code. The step initializes both counters and state
five only after these predicates. See the owning
[client handshake contract](usbinterface-FUN_180025400.md) for state, error and
re-establishment ownership.

At the selected-PSK full-init boundary `0x180020c38`, the worker calls the
three-attempt wrapper at `0x180020c49`. Nonzero return takes the absolute-value
comparison against `0x700003` at `0x180020c57`; only that identity error calls
the production-item cache clear at `0x180020c5e`. Every first-wrapper failure
then calls the wrapper once more at `0x180020c6d`. The resume boundary repeats
the same branches at `0x180020e7c`, `0x180020e8e`, `0x180020e95` and
`0x180020ea4`. Thus either `deviceInit` route permits six handshake attempts,
in two groups of three, with no sensor/USB reset, chip rediscovery, calibration
reload or PSK reselection between groups. Success exits the group loop
immediately. A second-wrapper failure leaves GTLS completion zero and takes
the route-specific postlude, not another whole-device initialization. The
full-init postlude still publishes initialized byte `+0x110`; see the
initialization-predicate boundary below. The direct restart entry
`FUN_1800210c0` has one wrapper call, hence at most three attempts.

## Initial Image-Base Path

- On the first/full initialization branch (`device_context +0x110 != 1`), it
  calls `FUN_18000e9b0` (`device_enable`). Profile 9 reaches
  `FUN_1800162ac`, which allocates the HAL buffers and installs
  `MilanHV_update_allbase` at HAL slot `+0x180`.
- `device_enable` calls `thunk_FUN_18001b6c8(0, NULL)` at `0x18000ea34..0x18000ea38`
  before chip-ID/profile selection. `FUN_18001b6c8` (`Reset`) sends the
  sensor-reset request through category `0x0a`, command `1`, with payload bytes
  `01 14`; no USB bus reset is issued by this path. `device_enable` then calls
  `Sleep(10)` before chip-ID/profile discovery. This is a fixed post-reset
  delay, before the later base-acquisition action.
- After sensor check and GTLS handshake, it issues action `0x0a` at
  `0x180020cd9`, then action `0x0c` at `0x180020d04`.
- Action `0x0a` dispatches `FUN_18000d24c` to load persisted base data.
  If its file-read helper returns `-1`, it clears HAL byte `+0x231` at
  `0x18000d350`, frees the temporary buffer, and returns zero. This missing-file
  route does not publish image validity, acquire a sensor sample, or arm FDT;
  initialization proceeds to action `0x0c`.
- `FUN_18000e1f0` action `0x0c` invokes HAL slot `+0x180`; for profile 9 this
  is `FUN_180015c60` (`MilanHV_update_allbase`).
- A valid persisted-base load does not suppress action `0x0c` either.
  `FUN_18000d24c` can restore the image and FDT buffers, set persisted-base
  byte `+0x231`, and install FDT stores through slot `+0x68`. The worker still
  proceeds to action `0x0c` whenever the device remains active and is not
  stopping; it does not branch on the loader's return or `+0x231`.
- A valid persisted load copies file image bytes into the allocation referenced
  by `+0x248`, but sets only persisted-base byte `+0x231`. It does not set
  base-valid `+0x232` or image-valid `+0x237`; those remain clear from
  `FUN_1800162ac` until action `0x0c` admits a fresh hardware acquisition.
- The loader is `GFCheckbase_isexist` (`0x18000d24c`), not the DAC-register
  helper `0x180005200`. Its file size is `0x24 + image_size(+0x250) +
  auxiliary_size(+0x260) + FDT_size(+0x270)`. It checks the trailing
  little-endian CRC against the preceding bytes, requires selected-OTP-valid
  dword `+0x1e8 != 0`, and compares the first 16 file bytes with selected OTP
  at `+0x205`. Admission copies FDT from file offset `0x20` into `+0x268`,
  then auxiliary data into `+0x258`, then actual image data into `+0x248`.
  It sets only `+0x231 = 1` and calls FDT setter `+0x68`; it restores neither
  current DAC/history nor validity/marker bytes `+0x232/+0x233/+0x236/+0x237`.
  A read failure clears `+0x231`; CRC or OTP-valid failure also removes the
  file. OTP identity mismatch removes the file but does not explicitly clear
  an already-set `+0x231`, so its retained-flag branch can still invoke the
  setter. Fresh HAL initialization has already cleared that byte.
- The worker does not inspect the action-`0x0c` return before continuing. This
  applies both to a zero-status validation rejection and to `-1` from the
  profile callback after a configuration or acquisition failure. Provided the
  active and power-stop gates still pass, initialization continues without an
  externally reported action-`0x0c` error and may continue without a valid image
  reference.
- After action `0x0c`, the active, non-stopping route calls
  `FUN_180017fe8` at `0x180020d32` (forwarding to `FUN_18001b15c`)
  with the 64-byte version destination
  at device context `+0x111` and timeout 2000. This is a category-`0x0a`,
  command-4 version query, not a manual FDT read. The worker then dispatches
  action `0x0e` at `0x180020dad` with mode 2 and timeout 200. That dispatch
  invokes profile callback `+0x40`; profile 9 sends category-6 sleep mode.
  Neither continuation reacquires an FDT sample or checks image-valid state.
- The mode-2 action is the last device command on the successful full-init
  path. After it returns, the worker logs, writes initialized byte `+0x110 = 1`,
  and signals context event `+0x108` and global event `0x180084860`. It does not
  invoke `thunk_FUN_18001afec` (`EcControl`) or send category `0x0a`, command 7.
  The profile-9 action-`0x0c` owner and its configuration, manual-FDT and image
  callbacks likewise contain no EC-control call. EC control is reached from
  later power-policy and capture-request owners, not from this initialization
  sequence.
- The same final firmware-query and mode-2 sequence follows an action-`0x0c`
  configuration or acquisition failure when the inter-operation gates remain
  open. There is no failure-specific sleep or EC-control postlude around the
  action: the common mode-2 command is the only power-mode transition.

Both the final version result and mode-2 result are ignored. The active/stop
tests after the final query, rather than its status, select the sleep tail;
the instructions after the sleep call publish initialized state without a
result test. A query failure retains the earlier 64-byte version snapshot.
Successful final query replaces it and the shared response-cache prefix.
Version snapshot consumers include `FirmwareVersionFunc` (`0x18001ca70`),
`GetDumpDataFunc` (`0x18001ce10`), and the version response at
`FUN_18001e1e8:0x18001e807..0x18001e836`. These are diagnostic/control
outputs; the image-base callback and engine image payload do not receive that
snapshot. The optional updater consumes the earlier probe snapshot only.

The inter-operation predicates are process byte `0x1800600a4 != 0` and the
byte at `*(device_context +0x160) +0x68e0 != 1`. They are tested after action `0x0c`
at `0x180020d09..0x180020d24` and after the final version query at
`0x180020d70..0x180020d8b`. Either failed predicate branches to
`0x180020b1d`, which signals only context event `+0x108`; it bypasses the
mode-2 command, initialized-byte write, and global initialized event. The
action return in EAX is not tested at either boundary. Mode-2 return likewise
has no status test before the initialized publication at `0x180020de6..0x180020e01`.

### Saved-Base Restore In An Already-Enabled HAL

Action `0x0a` during cold initialization is not the loader's only context.
`GFESD_procedure` (`0x18000d660`), dispatched by action 2, requires enabled
HAL byte `+0x204 != 0`. In profile 9 its reset/IRQ mask is `0x410`. Once its
bounded reset/chip-ID sequence finds the retained chip ID, the join at
`0x18000d817` requests mode 4, calls `0x18000d24c` at `0x18000d82f`, then arms
FDT-down through `+0xb0(1)`. It does not call all-base acquisition, re-read OTP,
reseed DAC, or change `+0x232/+0x233/+0x236/+0x237`. Thus an already-valid
image remains flagged valid when this loader replaces its bytes; a missing
file preserves the old image/validity while clearing the persisted-file flag.
The three repair-call statuses are ignored. Exhausted recovery instead sends
type-one reset; it does not run this restore join. Both paths store `+0x200 =
0x10`. The reset retries/status details are owned by
[the transaction note](usbinterface-FUN_180018dd8.md).

The saved image is not a clean-session certificate or a consumed token.
`GFSaveCaliPara` (`0x18000da18`) packs selected OTP bytes `+0x205..+0x224`,
FDT `+0x268`, auxiliary `+0x258`, image `+0x248`, and a trailing CRC. At
`0x18000dc0c` it calls the file writer `0x180017428` with mode `wb`, then
frees scratch at `0x18000dc14` without testing the writer result. Its caller
`0x180015c60` has already admitted the image and validity before invoking this
void save owner. The later `0x18000d24c` loader checks the file's CRC/OTP, not
the result of the latest save. Consequently an older complete file surviving
a failed save can be loaded by enabled-HAL ESD repair; a truncated file instead
fails the CRC/read boundary. Neither owner consumes or deletes a valid file
merely because it has been loaded. This permits retained coherent checkpoints,
but does not promise the latest unsaved state after failure or process loss.

Neither the saved layout nor its loader compares current high DAC or adjustment
history. All-base acquisition passes the low-DAC field at HAL `+0x310` to
`+0x158` for both reference images (`0x180015c60`); `0x1800074bc` encodes
the supplied DAC word in the image request and calls `0x180007c84` only after
successful image delivery. Live high-DAC/history is a separate evolving owner.
The retained reference must keep its low-DAC/OTP/configuration interpretation,
not equality with the high-DAC/history at its last file publication.

This conditional repair route differs from cold initialization, whose HAL
constructor has cleared validity before the same loader runs. It also differs
from initialized D0 entry, which invokes neither reset nor the loader. The
event worker maps event `0x12` to action 2, but the installed profile-9 FDT
parser publishes down/up/reverse and unknown-event codes, not `0x12`; the
repair dispatch is not a per-capture acquisition rule.

The optional two-image startup health acquisition and its later enrollment
consumer are owned by [the sensor-health contract](usbinterface-FUN_180011b9c.md).

### Retained Reference Consumer Boundary

The retained host reference is not an implicit firmware calibration handle.
The profile-9 saved-base setter `0x180005950:0x180005959..0x1800059af`
copies its 24-byte argument into software stores `0x180060718`,
`0x180060730`, `0x180060748`, and `0x180060778`, then returns without a
device command. At the ESD repair join `0x18000d817..0x18000d83d`, mode 4
uploads configuration, the loader installs these stores and image bytes, and
the down-arm callback consumes `0x180060730`. No image acquisition, FDT
measurement, or upload of the retained image occurs between reset and arm.
The arm request explicitly supplies all 24 base bytes; the later live-image
request supplies TX/HV and current DAC rather than a reference handle.

This repair join retains validity, drift anchor and pending setup marker.
The initialized D0-entry GTLS path independently reconstructs the client
session without touching those fields. Configuration and session establishment
therefore have no host-side requirement to produce another reference pair.
Command completion does not read back analog calibration or prove firmware
response behavior; that physical boundary is distinct from the explicit host
inputs to the next arm and image request.

The state consumed after ordinary retained operation is more detailed than the
saved-base file. A rejected temperature refresh preserves image `+0x248` and
image-valid `+0x237`, can leave base-valid `+0x232` zero, and can replace the
FDT calibration and four software stores. Event parsing subsequently evolves
the down/up/manual stores independently; the software drift anchor and marker
have separate owners. Thus the last admitted image and current FDT/validity/
anchor/marker tuple need not describe one successful acquisition. Restoring
every FDT store from the last admitted acquisition would not reproduce this
consumer state. See [the acquisition owner](usbinterface-FUN_180015c60.md)
and [the event loop](usbinterface-profile9-fdt-event-loop.md).

The hardware tuple also does not encode engine readiness. A newly attached
engine with context byte `+0x7c == 0` initializes from the next delivered
hardware reference even when marker `+0x236` is zero; an initialized engine
repeats setup only for marker one. The instructions at
`GoodixEngineAdapter.dll:0x18001fec0..0x18001fef2` implement these independent
predicates. Neither a file-presence flag nor a consumed marker can substitute
for engine initialization. [The sample adapter](FUN_18001f610.md) owns that
boundary.

### Composition With Power Loss And Resource Teardown

At the retained-reference consumer boundary, power exit/entry changes reader,
stop, request-mode and handshake state, not the admitted image/FDT tuple.
The previous D-state argument is diagnostic-only in `0x180022d70`; the
recorded system-power state selects handshake handling, not reference expiry.
The reset/configuration repair at `0x18000d817..0x18000d83d` independently
reinstalls configuration and explicit FDT command bases without acquiring a
reference. `0x180007ee0` replaces GTLS state without consuming that tuple.
Their retained image consumers have no additional sleep epoch or prior
USB-close-result input.

The host composition is consequently defined by the state at the next arm:
the same selected sensor/OTP/configuration interpretation, an admitted image
and correlated FDT/validity/anchor/marker tuple, reconstructed protocol/GTLS
resources, fresh command readiness and a newly supplied arm base. A successful
reset/configuration/session reconstruction does not depend on preserving the
old reader, firmware arm, cryptographic counters or USB handle. Sleep or a
failed resource close alone does not supply an image-invalidating predicate.
The reset transport (`0x18001b6c8`) changes neither the host image nor its
validity; cold constructor clearing is an allocation-initialization action,
not a measurement that the prior optical reference has ceased to be usable.

This is a composition of explicit host contracts. There is no recovered
additional firmware field that a reference capture must restore after power
loss, but the DLL does not expose firmware-internal power-on state or prove
the analog response to the composed sequence. Power loss followed by full
successful reconstruction is therefore covered at the host-input boundary,
not asserted to have measured firmware equivalence. Failed reconstruction
does not establish this boundary and cannot authorize live reuse.

## Full-Initialization Source Map

`drivers/goodix53x5/device/session.c:goodix_open_ssm_handler` combines this
worker with the selected profile's reset/chip/OTP/configuration handoffs.
`goodix_probe_ssm_handler` implements the early ping/version loop;
`goodix_gtls_retry_handler` implements selected-key handshake retries;
`device/base.c:goodix_base_ssm_handler` owns the primary base acquisitions.
`goodix_chip_ssm_handler` implements the initial reset/10-ms delay and chip
recovery described above. `GOODIX_OPEN_FINAL_FIRMWARE` implements the final
version refresh through the same shared-cache getter as the startup probe.
The firmware update and the two `send_mcu` post-send 2-ms delays have no
corresponding states.
The 100-ms failed-probe delay and 10-ms failed-handshake delay are represented.

`goodix53x5.c:goodix_open` starts the session state machine with
`open_usb_reset_required` false. After exclusive claim it runs the firmware
probe, reset/chip discovery, OTP selection, selected-key validation, fresh GTLS,
configuration and base acquisition. `GOODIX_OPEN_PARSE_OTP` reseeds current/default
calibration; module-static DAC history remains owned by live capture across
reconstruction (see `usbinterface-FUN_180007c84.md`). There is no cached-startup or hardware
checkpoint route. Bus reset belongs to whole-open recovery and explicit
reinitialization. Joined close and reinitialization retain only algorithm
process-state source through `device/base.c:goodix_milan_generation_retain_process`;
they release the hardware reference and consumed setup image. Native retained
HAL/D0 ownership has no corresponding cross-open hardware owner in these paths.
`goodix_milan_persistence_restore` remains the independent
engine-preprocessing import, not `GFCheckbase_isexist`'s hardware image/FDT set.

The native postlude's result classes are:

| Boundary | Native continuation with live entry gates |
| --- | --- |
| Enable or sensor-check failure | Request mode 2, disable HAL, signal context event; do not set initialized byte. |
| Two failed GTLS wrapper groups | Request mode 2 and publish initialized byte/events; skip persisted load, base acquisition, and final version query. |
| Base action success, rejection, or hard failure | Ignore action status, query final version, request mode 2, publish initialized byte/events. |
| Final version or mode-2 failure | No status-specific failure branch; preserve the preceding row's continuation. |

The Linux completion counterparts are `goodix_open_ssm_done`,
`goodix_open_complete_after_idle`, and `goodix_cleanup_failed_open`.
On failure, `goodix_open_ssm_done` calls `goodix_transport_quiesce` before
`goodix_open_complete_after_idle` invalidates transport, retains algorithm
process state, clears persistence/key ownership, and either performs one full
USB-reset retry or reports failed open. Success calls the completion owner
directly, preserving the physical reader and its partial assembly.
Cold GTLS exhaustion sets `open_gtls_failed` and suppresses that full-reset
retry. Cancellation/removal do not select that retry. The retry performs cold
reconstruction and another reference acquisition; it is a Linux recovery owner,
not the native initialized worker's failure branch.
`goodix_open_native_result` handles ordinary configuration exhaustion by
leaving FDT validity clear, setting initial recovery pending, and skipping the
manual/image sequence. Both that exit and ordinary first-manual or later
all-base failure reach `GOODIX_OPEN_FINAL_FIRMWARE`, then
`GOODIX_OPEN_FINAL_SLEEP`. The cold base child inserts no sleep or EC-off on
these returns. Final query success replaces the version string from the
64-byte shared cache; ordinary exhaustion preserves the previous snapshot.
Final sleep success or ordinary exhaustion publishes requested mode 2 through
`goodix_command_done` and completes initialization without EC-off. Neither
tail status grants reference validity or seeds base health. The callback and
tail entry states check cancellation/removal; terminal protocol, reader and
ownership errors retain failed completion and joined transport teardown.
The separate `GOODIX_OPEN_SLEEP`/EC-off states are defensive host-error cleanup,
not the normal native tail. An initial terminal base error runs child cleanup
before propagating; a terminal configuration error precedes
`open_ref_powered`, so parent cleanup skips sleep.
Successful open then enters `goodix_session_start_service` and the idle mode of
`goodix_scan_coordinator_handler`, without an initial down-arm command.
This cold continuation maps the native ignored all-base/final-tail status
decisions; full-reset recovery and retained HAL lifetime remain separate owners.

`goodix_session_suspend` uses `goodix_session_quiesce` to join the selected
service/action owner, CPU work and physical reader. `goodix_suspend_service_joined`
then starts sleep/EC-off with a fresh session cancellation token; those commands
start reception again. `goodix_suspend_power_done` joins that reader before
`goodix_suspend_joined` invalidates transport, releases hardware/setup frames and
completes suspend. `goodix_session_resume` invokes full reconstruction through
`goodix_maybe_start_reinit_subsm` before restarting service and completing resume.
Successful resume preserves the reconstruction reader; failure joins it before
invalidation and error completion. This source path reacquires hardware state
rather than taking the retained native resume branch below. Close uses
`goodix_session_quiesce` before releasing the interface.

## Resume Branch

- The byte comparison at `0x180020a1b` selects resume only for exactly
  `device_context +0x110 == 1`. This branch skips the firmware-version loop,
  `device_enable`, sensor check action `9`, persisted-base action `0x0a`,
  refresh action `0x0c`, the final version query, and mode-2 action `0x0e`.
  It performs no sensor reset, profile discovery, configuration upload,
  power-mode restoration, FDT-store installation, or image-base validation.
- At `0x180020e62`, a signed dword comparison of context `+0x168` against two
  controls GTLS initialization. Values below two go directly to completion.
  This is the stored system-power state after D0-entry normalization, not an
  unmodified callback argument; see `usbinterface-FUN_180022d70.md`.
- Values at least two invoke `FUN_180007ee0` with the retained ring pointer
  at context `+0x190` and handshake-complete dword at `+0x198`. On nonzero
  return, the worker calls `FUN_18001b94c` only when the 32-bit absolute error
  is `0x700003`, then invokes `FUN_180007ee0` once more for any first error.
  The cache-clear helper zeros its two protocol cache buffers and counts;
  it is not a sensor reset or HAL/base teardown.
- `FUN_180007ee0` serializes on the GTLS critical section, requires a non-null
  ring and completion pointer, clears the completion dword and ring bytes
  `+0x0c..+0x13`, and initializes/handshakes the client. It sets completion to
  one only on success. Thus this route retains the resource allocation but
  reinitializes the session rather than merely validating an old session.
- Success, or the below-two route, signals global event `0x180084860`.
  A second nonzero handshake return exits without signalling that event.
  Neither route rewrites `+0x110`, signals context event `+0x108`, destroys
  resources, or falls back to full initialization. All exits restore the
  init-thread sentinel to `-1` and return thread result one; that result alone
  is not a handshake-success indication.
- HAL allocation, callback table, calibration, retained image/FDT buffers,
  and base-valid bytes remain as they were. In particular, resume does not
  establish that an already-clear image-valid byte became valid. Hardware
  configuration continuity is not measured by a readback here. Explicit action
  3 invokes `FUN_180015710`, rebuilding/uploading mode-4 configuration before
  arming FDT-down, without refreshing bases. Ordinary `OnCaptureData`
  (`0x180021978`) instead calls `device_get_data` (`0x18000ebcc`), which sends
  EC control and directly invokes the down/up arm callback. That path does not
  dispatch action 3 or unconditionally request mode 4. Its arm wrapper can
  download configuration when the first arm status satisfies `(status & 3) ==
  3`. The explicit action-3 contract is in `usbinterface-FUN_18000e1f0.md`;
  neither route is part of resume-worker completion.

## Retained Startup Metadata And Session Boundary

Full initialization copies the selected 32-byte HAL OTP at `+0x205` into
device context `+0x36` before its first handshake. The version snapshot is
the separate 64-byte destination at device `+0x111`. The initialized branch
does not compare either snapshot against a new device read. For example,
`FirmwareVersionFunc` (`0x18001ca70`) answers from `device+0x111`, without
calling the wire-version getter. These retained values are distinct from
the evolving live DAC and from per-handshake keys and counters.

The resume route's below-two system-power case depends on retained session
ownership; it is not a protocol for reconstructing missing keys in a new
process. The at-least-two route instead calls the complete GTLS initializer,
which clears the previous client state before sending a fresh hello, while
preserving the HAL and its configuration interpretation. With a retained valid
selected PSK, no chip, OTP, PSK hash, version or configuration readback is
interposed before this handshake. Selected-PSK validity independently determines
whether the key getter needs production reads. See
[the client owner](usbinterface-FUN_180025400.md) for the unconditional context
reset and [configuration](usbinterface-FUN_180005094.md)
for ordinary first-arm repair. Neither route includes a device-queue drain
command or establishes that the MCU has no pending old packet.

## Scheduling

Entry requires nonzero global byte `0x1800600a4`. The full-init path checks it
again, together with nested context power-stop byte `+0x68e0 != 1`, between
the GTLS handshake, action 10, action `0x0c`, the final version query and the
final sleep/initialized-publication path. A failed later gate signals context
event `+0x108` without storing initialized byte `+0x110 = 1`; a failed entry
gate exits directly. Reader failure clears the global; subsequent successful
read completion does not restore it. D0 entry sets it to one before starting
the reader. See [reader failure ownership](usbinterface-profile9-fdt-event-loop.md).

- `FUN_180022d70` (`usbEvtDeviceD0Entry`) creates this worker whenever the
  global init-thread handle is the sentinel `-1`.
- The distinction between first initialization and resume is the persistent
  device-context initialized byte, not the biometric operation type.

## Initialization Predicate Boundary

Constructor return zero and HAL-enabled are not complete resource-validity
predicates: the selected constructor can return success after worker-event or
retained-buffer allocation failure. The exact allocation/publication ordering
is owned by `usbinterface-FUN_1800162ac.md`. The successful-resource contracts
here require the actual buffers/events, not merely the initialized byte.

This function consumes device-context byte `+0x110` as the full-initialization
versus resume predicate. The full-initialization tail sets that byte to one at
`0x180020ded`, after the mode-2 action and before signalling completion events.
Image-base validation rejection does not bypass that publication.

The same final sleep/publication block is also reached from
`0x180020cae` when both initial three-attempt GTLS wrapper calls fail after
`device_enable` has created the HAL. This edge precedes actions `0x0a` and
`0x0c`: it requests mode 2 and writes `+0x110 = 1` at `0x180020ded` while the
fresh HAL image-valid and base-valid bytes are still clear and no hardware base
has been acquired. A later D0 entry consequently takes the resume branch and
does not repair that missing base during initialization.

By contrast, `device_enable` failure or a nonzero action-9 sensor-check result
reaches `device_disable` at `0x180020b8a` after requesting mode 2. If profile 9
had been enabled, its close callback frees and nulls `+0x248`; these failure
paths do not publish `+0x110 = 1`.

## Initialized-Byte Lifetime

`FUN_1800232a8`, invoked by the framework device-add callback
`FUN_1800241c0`, writes device-context byte `+0x110 = 0` at
`0x180023467` during device-object initialization. The full `deviceInit`
tail writes one at `0x180020ded`. `usbEvtDeviceD0Entry` only tests the byte
at `0x180022e1f`; neither D0 callback clears it.

This is device-context state, not a per-capture or per-client flag. While the
same initialized context and HAL resources remain alive, another D0-entry
worker takes the resume branch rather than repeating action `0x0c`.
`usbinterfaceEvtDeviceReleaseHardware` does not clear `+0x110` either, but
destroys the HAL/base and GTLS resources. The byte alone therefore does not
establish resource validity after hardware release. See
`usbinterface-FUN_180023c40.md` for that distinct teardown boundary.

Request termination is separate from this lifetime. Queue dispatch
`FUN_180024220` routes request code `0x440008` to `FUN_18002296c`
(`OnReset`). With sufficient output capacity, that handler calls
`FUN_18001ff08` (`CompletePendingRequest`) with cancellation status and
returns an eight-byte successful reset response. It issues no sensor reset,
does not call `device_disable`, and does not clear `+0x110` or the base.
`CompletePendingRequest` only completes/unmarks the pending request and clears
its `+0xf8` owner when applicable.

The capture cancellation callback `FUN_180020ef0` (`gfOnCancel`) likewise
sets stop bytes `+0x154/+0x152` and completes/clears the pending request,
without clearing `+0x110`, freeing the retained base, or destroying GTLS.
These request callbacks are not the framework hardware-release callback;
their invocation alone does not cause another full `deviceInit`.
