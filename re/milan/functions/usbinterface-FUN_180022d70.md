# usbinterface.dll FUN_180022d70

## Identity

- Address: `0x180022d70`
- Logged name: `usbEvtDeviceD0Entry`
- Role: UMDF device-power entry callback.

## Findings

- Clears the HAL/device stop byte at device-context nested offset `+0x68e0`.
- Resets the initialization event when initialization is not already complete.
- Restarts the continuous USB read pipe.
- Launches `FUN_180020970` (`deviceInit`) when the global init-thread handle is
  `-1`.
- It neither clears HAL image-base validity bytes `+0x232/+0x237` nor directly
  invokes `MilanHV_update_allbase`.
- It does not call `EcControl`, request a sensor mode, or insert a sleep before
  launching the initialization worker. Its hardware-side preparation is the
  continuous-read-target start.
- A first D0 entry reaches full initialization and action `0x0c`; an ordinary
  resume reaches `deviceInit`'s resume branch and preserves the base.

The `startInitDeviceMonitoring` diagnostic at `0x180022efd` names this same
launch block. Its `_beginthreadex` call at `0x180022f39` installs
`0x180020970` with creation flag 4, then resumes the returned handle. There is
no separate monitoring callback or periodic polling loop behind that label.
`deviceInit` performs the bounded initialization/resume route, restores the
thread-handle sentinel and returns. The other `_beginthreadex` caller is HAL
worker startup at `0x18000e15d`; `CreateThread` at `0x180021384` instead launches
the image-error-driven GTLS restart. These are distinct from the WDF live-frame
expiry timers and the independent continuous-reader callback.

Reader-start failure is not an internal initialization-thread suppression
predicate. The signed-negative branch at `0x180022eb0..0x180022ed3` stops
the read target with action one and then rejoins `0x180022ed9`. It still sets
`0x1800a2109 = 1` and, if the initialization-thread handle is `-1`, attempts
to create/resume `deviceInit`. The function finally returns the original
reader-start status saved in `EDI`, not thread or GTLS success. The framework's
reaction to that error is a separate lifetime boundary.

### Receive and first-command ordering

The call through WDF slot `+0x350` at `0x180022ea8` starts the continuous-read
target before `_beginthreadex` at `0x180022f39` creates the initialization
worker. This is start-before-send ordering, not a wait for the first receive
completion or a category-C wake notification. The initialized worker tests
signed system-power state `+0x168 >= 2` at `0x180020e62..0x180020e69` and
calls the GTLS wrapper directly at `0x180020e7c`. It has no category-C readiness
gate, preliminary vendor command or fixed pre-handshake settling delay.

`drivers/goodix53x5/device/session.c:goodix_resume_warm` reclaims the USB
interface and starts `goodix_gtls_retry_handler`. Its first hello reaches
`device/transport.c:goodix_transport_send`, which calls `goodix_reader_start`
before submitting bulk OUT, matching the native start-before-send ordering. The
physical reader then remains posted during the ACK wait. ACK publication before the OUT callback is retained independently
in `goodix_rx_cell`; callback order alone does not discard an admitted ACK.

Category-C/1 publishes worker event `0x13` through `0x180019264`; it neither
satisfies a sender ACK nor gates the initialization worker. The screen-off
consumer can request a WOF image, while the screen-on consumer `0x18000dc84`
rearms the retained FDT wait direction. See
[notification publication](usbinterface-FUN_180018dd8.md#unsolicited-category-c-notifications)
and [GTLS ACK/retry ownership](usbinterface-FUN_180025400.md).

## Stored Power State

`usbEvtDeviceD0Exit` (`FUN_180022ff0`) records the framework-reported system
power state in context dword `+0x168`. Before restarting the pipe or launching
the worker, D0 entry tests global byte `0x1800e2120`: when it is not exactly
one and signed `+0x168 > 1`, it writes `+0x168 = 0` and global byte
`0x18005f398 = 1`. Otherwise it preserves `+0x168`.

`DriverEntry` (`FUN_18002409c`) initializes that global from
`FUN_1800176e0` (`GetPowerInformation`). The helper queries power-information
level 4 and returns output byte `+0x14`, logged as Modern Standby support;
it returns zero when the query fails. This is a host capability value, not a
sensor-configuration or resource-continuity indicator.

Consequently, an initialized worker's handshake predicate is the normalized
state. With global `0x1800e2120 != 1`, a previously recorded state greater
than one does not itself cause a resume handshake. With that global equal
to one, the recorded state remains available to the worker's signed
greater-than-one test. This path contains no sensor-configuration readback
or base validation. See `usbinterface-FUN_180020970.md` for worker completion
and failure behavior.

## Lifetime Consequence

### Framework idle and wake policy

`PrepareHardware` (`0x180023600`) retrieves USB target traits and calls
`SetPowerPolicy` (`0x180020494`) only when `traits & 2 != 0`, the WDF
`WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE` bit. Failed trait retrieval stores
zero and skips that call. This is a USB capability predicate, independent of
sensor profile selection.

For Modern Standby capability `0x1800e2120 == 1`, `0x18002056d..0x1800205b1`
passes the following `0x24`-byte idle settings to
`WdfDeviceAssignS0IdleSettings`, WDF table slot `+0x80`:

| Offset | Field | Value |
| --- | --- | --- |
| `+0x04` | `IdleCaps` | `3`, `IdleUsbSelectiveSuspend` |
| `+0x08` | `DxState` | `5`, `PowerDeviceMaximum` |
| `+0x0c` | `IdleTimeout` | `5000` |
| `+0x10` | `UserControlOfIdleSettings` | `1`, disallow user control |
| `+0x14` | `Enabled` | `2`, `WdfUseDefault` |
| `+0x18` | `PowerUpIdleDeviceOnSystemWake` | `2`, `WdfUseDefault` |
| `+0x1c` | `IdleTimeoutType` | `2`, `SystemManagedIdleTimeoutWithHint` |
| `+0x20` | `ExcludeD3Cold` | `2`, `WdfUseDefault` |

The 5000-ms value is an idle hint, not a capture timeout. A negative idle-setting
result returns immediately. After success, exact device byte `+0x151 == 1`
or `+0x155 == 1` selects `WdfDeviceAssignSxWakeSettings`, slot `+0x88`, at
`0x1800205fc..0x180020651`. Its `0x14`-byte settings specify `DxState = 5`,
user control `1`, enabled `2`, and zero child-wake booleans. Negative results
propagate to `PrepareHardware`; success logs `RemoteWakeUp enable`.

Device construction `0x1800232a8` copies configuration `+0x434` to device
`+0x151` and configuration `+0x442` to `+0x155`. Capability one together with
exact configuration `+0x442 == 1` forces `+0x151 = 1`. The compiled defaults
are zero and one respectively. These fields also select the profile-9
screen-off image owner described in [the down handler](usbinterface-FUN_180014e10.md).

Construction calls `WdfDeviceInitSetPowerPolicyOwnership`, slot `+0xa8`, with
TRUE only when capability is not one. Capability one leaves framework-default
ownership. Its registered PnP/power table contains D0 entry/exit and hardware
callbacks; self-managed-I/O init/suspend/restart entries remain zero. These
settings request framework-managed USB idle/wake behavior; they are not a
vendor FDT command issued by D0 entry.

The DLL has no call through WDF slot `+0xa0`,
`WdfDeviceInitSetPowerPolicyEventCallbacks`; it does not install its own
`EvtDeviceArmWakeFromS0`, disarm or wake-triggered callback. USB wake policy
and the ordinary FDT/display owners are separate contracts.

### Sending while the device is idle

`USBSend` (`0x18001c73c`) calls `0x18001c698` before its send lock and
synchronous write when capability is exactly one and its retained device
handle is nonnull. This helper calls `WdfDeviceStopIdleActual`, table slot
`+0x7c0`, with `WaitForD0 = (0x180084852 != 1)`. D0 entry writes this byte
one before starting the reader; D0 exit clears it after stopping the reader.
A nonnegative stop-idle result causes an immediate
`WdfDeviceResumeIdleActual`, slot `+0x7c8`, before returning to `USBSend`.
A negative result skips resume-idle; the helper returns void and `USBSend`
still proceeds to its ordinary protocol-enabled check and write.

This is a per-send power-up request, not an FDT notification filter or an idle
reference retained for the whole capture. No command is sent by the helper
itself. A command-producing capture/display/worker owner can therefore cause
a D0 transition before its write; seeing that write after D0 entry does not
make D0 entry its command producer.

Selective-suspend and system-power D0 transitions are not equivalent to HAL
detach. The retained `+0x248` image base and its validity flags survive this
callback pair as long as WDF does not also schedule `ReleaseHardware`.
