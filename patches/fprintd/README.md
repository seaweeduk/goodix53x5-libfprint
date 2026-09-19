# fprintd Milan Overlays

`1.94.5-milan-update-save.patch` targets fprintd `v1.94.5` at exact commit
`b54a007ccf58ac0ae074c7151b223f35cbd17306`. It consumes the paired
libfprint terminal update boolean and synchronously saves the already-mutated
matched print through stock `store.print_data_save()`.

Verify saves the object held in `priv->verify_data`; identify saves the exact
match returned by libfprint. Saving occurs once after the match report and
before operation-owned state is cleared. A save failure is logged and discarded
without changing authentication. No storage API, file backend, transaction,
locking, retry, or persistence test subsystem is added.

Verify a pristine pinned checkout:

```sh
./patches/fprintd/verify-update-save-patch.sh /path/to/fprintd-v1.94.5
```

Regenerate deterministically from a modified checkout at the pinned commit:

```sh
./patches/fprintd/generate-update-save-patch.sh /path/to/modified-fprintd
```

The SHA-256 sidecar is authoritative and checked before apply validation.

## Retained hardware session

Apply `1.94.5-serviced-session.patch` **after** the update-save overlay. It
pairs with the libfprint idle suspend/resume patch and the Goodix driver's
background maintenance; the stack scripts build and install them together.

Upstream fprintd opens the sensor on `Claim` and closes it on `Release` or
when the client disappears, so every unlock repeats the driver's cold
initialization. With this overlay the first authorized `Claim` opens the
device and it then stays open, the way the native Windows service keeps its
hardware host resident:

- `Claim` on an already open device completes immediately; only the first
  claim (or a claim after the daemon restarted) performs the cold open.
- `Release` and client disappearance still cancel a running action and wait
  for it (and unset a completed verify/identify/enroll that was never
  stopped), but only end the user's authority; the hardware stays open and
  the driver keeps servicing finger-detection events between claims.
- A removed device is closed on `FpDevice::removed`, since libfprint withholds
  `FpContext::device-removed` until an open device has been closed; the
  manager's existing removal handling then unexports it.
- The device's `busy` property is also true while the hardware is open, so
  the manager never arms its idle exit while a sensor is retained. On
  `SIGTERM` or bus-name loss the manager ends any claim and closes each open
  device for real before storage is deinitialized.
- The manager's existing `PrepareForSleep` handling drives suspend and resume
  as before; the libfprint patch makes the idle-open device reach the driver.
  While the device is asleep or still resuming, a cold `Claim` open and one
  `VerifyStart`/`EnrollStart` are held and dispatched when resume completes,
  instead of failing with a busy device. `VerifyStop`, `EnrollStop`,
  `Release` and client disappearance fail a held start.

Nothing else in the daemon changes: authorization, claim ownership checks,
retries, status signals, storage and the update-save overlay behave as
upstream. A positive match is reported to the client as soon as the driver
has validated it; the driver finishes the sensor's deactivation on its own,
so `VerifyStop` and `Release` return promptly.

A hardware fault while idle is handled inside the driver: it stops background
maintenance and the next action reconstructs the sensor session, so no daemon
recovery path is needed. Resume after system sleep reconstructs the hardware
session before the held start runs; that conservative cold path is a known
difference from the native retained-power resume.

Verify composition and reverse application, or regenerate only this layer:

```sh
./patches/fprintd/verify-serviced-session-patch.sh /path/to/pristine-fprintd
./patches/fprintd/generate-serviced-session-patch.sh /path/to/modified-fprintd
```

The generator diffs `src/device.c`, `src/manager.c`, `src/main.c` and
`src/fprintd.h` against the update-save layer using a temporary index; it
neither commits the source checkout nor alters its index. The stack builder
checks both patch digests and includes both in its overlay identity.
