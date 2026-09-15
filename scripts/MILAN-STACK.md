# Milan Source Installation

These scripts build the Milan driver with pinned, patched libfprint and fprintd,
then install the pair into normal system paths. The source installer checks file
ownership; it does not install, remove, or restore distribution packages.

## Layout

| Distribution family | libfprint directory | fprintd executable |
| --- | --- | --- |
| Arch | `/usr/lib` | `/usr/lib/fprintd` |
| Debian/Ubuntu | `/usr/lib/<multiarch>` | `/usr/libexec/fprintd` |
| Fedora/RPM-based | RPM's configured library directory, usually `/usr/lib64` | `/usr/libexec/fprintd` |

Other systemd distributions use `/usr/lib` and `/usr/libexec/fprintd` by default.
Manual installation requires a supported ownership-query tool: `pacman`,
`dpkg-query`, or `rpm`. Debian-family builds also use `dpkg-architecture`.

The full fprintd runtime includes the four `/usr/bin/fprintd-*` commands, PAM
module, systemd service, D-Bus and polkit files, translations, and manuals.
The PAM module is installed into the library directory's `security/` subdirectory;
enabling fingerprint authentication in the system's PAM configuration is a
separate user action. The driver build selects `goodix53x5` only.

Build metadata and the installed-file inventory live in
`/usr/share/goodix53x5-milan`. `build.env` records the actual `LIBRARY_PATH`,
`DAEMON_PATH`, and `LIBDIR`, along with build provenance. Print state and the
imported Windows PSK remain under `/var/lib/fprint`.

There is no executable-selection or `LD_LIBRARY_PATH` drop-in. On systems with
active SELinux and `restorecon` available, installation applies the existing
policy's labels to installed files and newly created directories. It does not
install SELinux tools or define custom policy. This is not confirmation that any
particular SELinux report is fixed.

## Build And Install

Build as your normal user, then install as root:

```sh
stack_root="${GOODIX_MILAN_STACK_ROOT:-${XDG_STATE_HOME:-$HOME/.local/state}/goodix53x5-milan}"
./scripts/build-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --build
sudo env GOODIX_MILAN_STACK_ROOT="$stack_root" ./scripts/status-milan-stack-local.sh --preflight
sudo env GOODIX_MILAN_STACK_ROOT="$stack_root" ./scripts/install-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --installed
```

`./install.sh` combines build and installation. Release is the default; use
`./install.sh --debug` or `GOODIX53X5_DEBUG=1` on the builder for diagnostics.
Build dependencies include Python 3, gettext, Perl's `pod2man`, and the PAM and
libsystemd development files in addition to libfprint/fprintd's other dependencies.
Debug builds use the same matching, learning, and persistence behavior. Capturing
images requires the separate configuration in
[`DEBUG-CAPTURE-GUIDE.md`](../DEBUG-CAPTURE-GUIDE.md).

Preflight reports package-owned destinations, unmanaged existing files, or
changes to a previous source installation. Resolve the reported conflicts before
installing. It does not remove packages or force overwrites. An installation
created by these scripts can be updated by rebuilding and installing again.

The default build root is `$XDG_STATE_HOME/goodix53x5-milan`, or
`$HOME/.local/state/goodix53x5-milan` when `XDG_STATE_HOME` is unset. Override it
with `GOODIX_MILAN_STACK_ROOT`; it must be absolute and persistent, not under
`/tmp`, `/var/tmp`, or `/run`. The commands above pass the selected root through
sudo explicitly, including when `XDG_STATE_HOME` is customized.

The builder uses pristine pinned sibling sources when available, otherwise
checkouts below the build root. Explicit source locations are accepted through
`GOODIX_MILAN_LIBFPRINT_SOURCE` and `GOODIX_MILAN_FPRINTD_SOURCE`.

Each build verifies the pinned patches and runs the existing libfprint update,
Milan synthetic, state, runtime, and transport suites. It stages files under
`builds/<publication>/payload/` and publishes `builds/current` only after the
payload inventory and runtime files pass verification. Building never installs
files or changes the service. Packaging can consume the staged filesystem without
running the manual installer's package-ownership checks.

## Update And Remove

To update, select the desired source revision and repeat the build/install steps.
The installer checks the old inventory, replaces its own files, and removes
obsolete files from that inventory. Both components are updated together.

```sh
./uninstall.sh
./scripts/status-milan-stack-local.sh --absent
```

Uninstall deletes only recorded, unchanged files. It preserves shared directories,
print state, and the imported Windows PSK. It does not restore packages or restart
a removed daemon. Install distribution packages separately if desired.

Installation and removal temporarily runtime-mask fprintd so D-Bus cannot start
it while files change. Installation refreshes labels and the linker cache,
verifies the selected daemon/library, and starts fprintd. Failures after file
changes leave the service blocked and print a diagnostic; files are not rolled
back. Inspect `--installed` and the reported failure before retrying. Removal
cleanup can be retried after files have been removed.

For a debug writer already configured while the service is masked, pass
`GOODIX_MILAN_CAPTURE_DIR` as the absolute dump directory. The installer creates
the campaign's `driver-build.json` from installed bytes before unmasking, then
checks that the service selects that exact directory. The debug capture guide
shows the complete sequence.

## Moving From The `/opt` Installation

Finish any active capture first. While still using the old checkout, run its
`./uninstall.sh` to remove `/opt/goodix53x5-milan` and its service-selection
drop-in. Then switch to the new checkout, resolve any conflicting package-owned
files, and install the new build. The new installer deliberately refuses to
install alongside the old layout.

Existing sealed campaigns keep their original manifests, including recorded
`/opt` paths. Historical validation and `--compare-current` do not require the
captured library to remain installed.
