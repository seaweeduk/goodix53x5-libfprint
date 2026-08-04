# Milan Paired Shadow Stack

These scripts build and install the native Milan driver together with its
pinned patched libfprint and fprintd. Package files under `/usr` are never
overwritten. Runtime files live under `/opt/goodix53x5-milan`; the only system
configuration owned by this repository is
`/etc/systemd/system/fprintd.service.d/98-goodix53x5-milan-stack.conf`.
Fingerprint state remains in `/var/lib/fprint` through
`StateDirectory=fprint`.

## Build And Install

Release is the default:

```sh
./scripts/build-milan-stack-local.sh
sudo ./scripts/install-milan-stack-local.sh
```

Build and publish a diagnostic payload instead:

```sh
GOODIX53X5_DEBUG=1 ./scripts/build-milan-stack-local.sh
sudo ./scripts/install-milan-stack-local.sh
```

Debug changes only compile-time diagnostics. Matching, learning, serialization,
and stock adaptive persistence are identical. Image dumping still requires a
separate systemd environment drop-in; the stack scripts do not install one.

Check or remove the stack:

```sh
./scripts/status-milan-stack-local.sh --build
sudo ./scripts/status-milan-stack-local.sh --installed
sudo ./scripts/remove-milan-stack-local.sh
sudo ./scripts/status-milan-stack-local.sh --packaged
```

The default build root is `$XDG_STATE_HOME/goodix53x5-milan`, or
`$HOME/.local/state/goodix53x5-milan` when `XDG_STATE_HOME` is unset. `sudo`
resolves the invoking user's home so the install command finds that published
build. Override it with `GOODIX_MILAN_STACK_ROOT` when needed; the root must be
absolute and persistent, not under `/tmp`, `/var/tmp`, or `/run`.

The build first uses pristine pinned sibling sources when present. Otherwise it
uses or creates pinned checkouts below the state root. Explicit source paths are
accepted through `GOODIX_MILAN_LIBFPRINT_SOURCE` and
`GOODIX_MILAN_FPRINTD_SOURCE`.

Every build verifies patch sidecars and apply context, runs the libfprint
update-result, Milan synthetic, and Milan runtime tests once, builds the paired
daemon, verifies daemon/library linkage, and publishes a checksummed payload
atomically. The manifest records
source commits, source trees, patch hashes, overlay inputs, script hashes, and
debug mode.

Install verifies the complete payload and merged service state before stopping
fprintd. A failed switch restores the previous owned prefix and drop-in and
restarts the previous service. Removal keeps the owned stack available until
packaged fprintd has restarted successfully, then deletes only owned files.
Unmanaged or modified paths are rejected.

Run the fake-root transaction checks without root or system changes:

```sh
./scripts/tests/test-milan-stack-tools.sh
```
