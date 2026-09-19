# usbinterface.dll FUN_180023c40

## Identity

- Address: `0x180023c40`
- Logged name: `usbinterfaceEvtDeviceReleaseHardware`
- Role: full UMDF hardware-release callback.

## Findings

- Marks the nested HAL/device context stopped. It waits for `deviceInit` only
  when its global handle `0x1800600a8` is not `-1` and byte `0x1800600a4`
  equals one. The first wait is 3000 ms; timeout (`0x102`) causes a second
  6000-ms wait. Handle close and restoration of the `-1` sentinel occur inside
  this conditional branch. Reader failure clears the byte and therefore skips
  this wait branch; profile disable and the following teardown remain outside
  it. See [reader failure and initialization gates](usbinterface-profile9-fdt-event-loop.md).
- Calls `device_disable` (`FUN_18000e8cc`) at `0x180023e27`.
- `device_disable` stops the HAL event thread and invokes the profile close
  callback; profile 9 reaches `FUN_1800160a0` (`milan_HVseries_disable`).
- Neither `device_disable` nor the profile-9 close callback requests a sensor
  mode or calls `EcControl`; hardware release adds no power command after HAL
  teardown.
- Destroys driver synchronization state, GTLS state, notification registration,
  and device initialization handles after HAL teardown.

### Worker Stop Is A Bounded Wait

`device_disable -> FUN_18000e184` writes worker event type `0x16`, clears
run byte `0x180061218`, and signals HAL event `+0x10`. It then waits on
thread handle `0x18005e788` for 20000 ms at `0x18000e1bc..0x18000e1c8`.
The next instructions do not test the wait result: a handle other than `-1`
is closed and the helper returns zero. The helper does not restore the handle
sentinel. `device_disable` then invokes profile close and deletes the action
critical section regardless of that wait result.

Worker `0x18000df20` tests the run byte only after finishing its selected
handler. Stop publication therefore does not interrupt an already selected
handler or a synchronous command wait. Event `0x16` has no action in that
worker's dispatch; it supplies a wake for the subsequent run-byte test. This
is distinct from deactivation event `0x15`, which dispatches EC power policy
and does not terminate the worker. A timeout return from the stop wait is not
proof that all references to HAL storage have ceased.

The release path's GTLS destruction `0x1800208c4` frees ring descriptor
`device+0x190`, closes event `+0x1a0`, deletes wrapper critical section
`0x180084868`, and closes global handshake event `0x180084860`. Neither this
helper nor `ReleaseHardware` waits on reader-created restart-thread handle
`0x1800a2110`. That thread owns the same device/ring inputs; its launch and
completion rules are in `usbinterface-FUN_180025400.md`. The separately
conditional initialization-thread wait cannot be treated as its join.

## Lifetime Consequence

Unlike D0 exit, this callback ends the hardware-context image-base lifetime.
The retained `+0x248` buffer is freed below `milan_HVseries_disable`.
`FUN_1800208c4` destroys the port GTLS state and its synchronization objects.
This callback does not clear the device-context initialized byte `+0x110`;
device-object initialization in `FUN_1800232a8` owns its explicit zero store.
A new device context consequently enters full `deviceInit`, but the release
callback alone does not establish that a subsequent entry using an old context
will take that branch.

The callback is registered by `FUN_1800232a8` as a framework hardware-release
callback, not as a client file-close callback. `device_disable` has direct
callers here and in `deviceInit` initialization-failure handling. D0 exit and
queue `usbinterfaceEvtIoStop` (`FUN_1800243c0`) do not call it. The latter
cancels or acknowledges a stopped request and clears pending request `+0xf8`,
without freeing the HAL image base or clearing `+0x110`.

`usbinterfaceEvtDeviceQueryRemove` (`FUN_180023b50`) and
`usbinterfaceEvtDeviceSurpriseRemoval` (`FUN_180023f00`) set the nested stop
byte and unregister the display-power notification through `FUN_18001782c`.
They do not disable the HAL, free `+0x248`, clear either validity byte, or clear
`+0x110`; actual destruction remains owned by a subsequent `ReleaseHardware`
callback. `usbinterfaceEvtQueryStop` (`FUN_180023ff0`) returns zero without any
of those mutations.

Device creation `FUN_1800232a8` registers PnP/power callbacks and the I/O queue,
but no file-object cleanup or close callback. The driver-context cleanup callback
`FUN_1800241d0` releases its separate global container through `FUN_18002d0a0`
and does not call `device_disable`. Client-file closure therefore has no direct
image-base mutation in this DLL; framework scheduling of request stop,
deactivation, device release, and driver cleanup is a separate boundary.

The profile-close chain `0x1800160a0 -> 0x18000445c` releases its allocations;
it does not mark the persisted reference invalid or delete its saved file.
Neither the saved-base loader `0x18000d24c` nor the next ordinary live consumer
`0x180014e10` reads a USB-handle-close result. The loader checks file integrity
and selected OTP; the live consumer checks image-valid `+0x237`, active mode,
screen and callback ownership. A close-result bit is not one of these inputs.
D0 exit likewise retains reference validity after its unchecked sleep/EC
results; see [D0 exit](usbinterface-FUN_180022ff0.md).

Allocation destruction is therefore distinct from optical-reference rejection.
A copied, admitted reference with its settled FDT/validity/anchor/marker tuple
does not become an invalid image because a later resource-close operation
fails. Capturing a settled tuple still requires that command, event and image
owners can no longer mutate it. That ownership condition is separate from the
result of releasing a USB interface or handle. A subsequent active consumer
requires newly valid transport/session/configuration ownership regardless of
the previous close result.

## Current Source Mapping

`drivers/goodix53x5/goodix53x5.c:goodix_close` calls
`device/session.c:goodix_session_quiesce` to join selected maintenance,
foreground/CPU ownership and logical transport work, then the physical reader
through `goodix_transport_quiesce`. Only afterward does `goodix_close_joined`
invalidate transport and release the USB claim.
`device/session.c:goodix_open_complete_after_idle` and
`goodix_reinit_idle_joined` likewise invalidate transport after the reader join
on failed open and before post-suspend reconstruction.
`goodix_suspend_joined` releases hardware/setup ownership after the separate
power-command reader join. Successful open, successful resume and ordinary
service/action handoff preserve the reader; they are not hardware teardown.
`device/base.c:goodix_milan_generation_retain_process`
frees the old setup frame but retains the process gain/classifier source in
`milan_retained_generation`; hardware teardown then frees the raw reference.
`goodix_finalize` destroys that retained generation. A replacement object has no
in-memory process transfer even in the same Linux process. Its first setup uses
the independently validated preprocessing subset or defaults through
`goodix_milan_generation_prepare_setup` and `goodix_milan_persistence_restore`,
after fresh hardware-reference acquisition. There is no hardware-reference
checkpoint, warm parking or cross-open FDT restoration in these source owners.

Native USB hardware release and engine detach are distinct callbacks. The
former frees the hardware reference through `FUN_1800160a0`; the latter clears
the engine workspace but leaves its DLL process globals alive, as documented in
`FUN_18001ed10.md`. Neither callback schedules module unload. A fresh native
module lifetime and an engine reattachment within the old module consequently
have different algorithm-state sources; client request completion alone selects
neither boundary.
