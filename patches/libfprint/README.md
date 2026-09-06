# Libfprint Update Result Overlay

`libfprint-update-result.patch` is an additive Patch A overlay for libfprint
`v1.94.10` at exact commit
`0c97a47d8ef405cd577b87058c1e89cae9d242e7`.

The patch is the narrow private contract shared by the repository's Milan
driver and pinned fprintd build. The driver mutates a validated matched raw
print in place, reports matches through the stock APIs, and supplies one update
boolean only at terminal verify or identify completion. Existing finish APIs
remain wrappers that ignore the boolean. One focused test covers positive,
no-match, and terminal-error propagation.

`scripts/build-local.sh` verifies and applies this patch immediately after the
pinned checkout and before copying the driver overlay or configuring Meson.
The build fails deterministically on a wrong revision, tracked source changes,
or patch context drift. Untracked build/overlay files are ignored because the
local checkout intentionally retains them between builds. To perform the same
check manually:

```sh
./patches/libfprint/verify-update-result-patch.sh /path/to/pristine/libfprint
```

Regenerate it deterministically from a modified worktree at the pinned commit:

```sh
./patches/libfprint/generate-update-result-patch.sh /path/to/modified/libfprint
```

The generator writes a base-revision header into the patch. The verifier
enforces that revision before running `git apply --check`; `git apply` alone
does not enforce patch metadata.

## Idle Suspend Notification

`libfprint-idle-suspend-notify.patch` targets the same exact revision and adds
the nullable internal `FpDeviceClass.suspend_idle_notify` callback. Core calls
it synchronously only for an accepted suspend of an open, idle device, before
completing suspend. The callback may only invalidate driver-owned memory: no
I/O, asynchronous work, blocking, main-loop reentrancy, action start or
completion calls. It has no paired resume callback.

Goodix opts in only to set `needs_reinit`; its existing next-action SSM performs
reinitialization. Closed devices and drivers that do not opt in retain their
existing behavior. Interactive hooks and the short-action suspend path are
unchanged. This does not establish physical S3, s2idle or S4 recovery.

Both local build routes verify and apply the patch, and include it in overlay
and production source identities. The stack manifest also seals its digest.
All drivers must be rebuilt against the changed internal class layout. Check
the patch against a pristine checkout with:

```sh
./patches/libfprint/verify-idle-suspend-notify-patch.sh /path/to/pristine/libfprint
```

One additional `fpi-device` case, `/driver/identify/suspend_idle_notify`, checks
notification ordering/counts, repeated cycles, and closed/non-opted-in controls.
The existing idle and interactive suspend compatibility cases are unchanged.
