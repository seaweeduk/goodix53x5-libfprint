# Milan Source Installation

These scripts build the Milan driver with pinned, patched libfprint and fprintd,
then install the pair into the distribution's normal paths under `/usr`. The
installer only ever touches files it recorded itself; it does not install,
remove, or restore distribution packages.

## Layout

| Distribution family | libfprint directory | fprintd executable |
| --- | --- | --- |
| Arch | `/usr/lib` | `/usr/lib/fprintd` |
| Debian/Ubuntu | `/usr/lib/<multiarch>` | `/usr/libexec/fprintd` |
| Fedora/RPM-based | RPM's `%{_libdir}`, usually `/usr/lib64` | `/usr/libexec/fprintd` |

Other systemd distributions use `/usr/lib` and `/usr/libexec/fprintd`. The
installer needs `pacman`, `dpkg-query`, or `rpm` to confirm that no package
owns a destination file.

The payload is the full fprintd runtime (the `fprintd-*` commands, PAM module
under `<libdir>/security`, systemd unit, D-Bus and polkit files, translations,
manuals) plus libfprint built with the `goodix53x5` driver only. Enabling
fingerprint authentication in PAM is left to you and your distribution. Print
state and the imported Windows PSK stay under `/var/lib/fprint`.

The first `Claim` opens the sensor; after that the daemon keeps it open across
`Release` and client exit, so later unlocks skip the cold initialization while
the driver services the sensor in the background. The paired libfprint and
fprintd patches are required together and support only this Goodix driver. See
the [fprintd overlay notes](../patches/fprintd/README.md#retained-hardware-session)
for the claim, sleep and shutdown behaviour.

`/usr/share/goodix53x5-milan/build.env` records the build (`LIBRARY_PATH`,
`DAEMON_PATH`, pinned revisions, debug flag and IDs) and `inventory.json` lists
every installed file with its hash. On SELinux systems the installer runs
`restorecon` over the files it wrote so they receive the policy's labels.

## Build And Install

Build as your normal user, then install as root:

```sh
./scripts/build-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --build
sudo ./scripts/status-milan-stack-local.sh --preflight
sudo ./scripts/install-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --installed
```

`./install.sh` runs the build and the install together; `./install.sh --debug`
(or `GOODIX53X5_DEBUG=1` for the builder) produces a diagnostic build with the
same matching and persistence behaviour. Image capture needs the separate
configuration in [`DEBUG-CAPTURE-GUIDE.md`](../DEBUG-CAPTURE-GUIDE.md).

Builds live under `$XDG_STATE_HOME/goodix53x5-milan` (default
`~/.local/state/goodix53x5-milan`); `sudo` resolves the invoking user's home.
Override with `GOODIX_MILAN_STACK_ROOT`, which must be absolute and not under
`/tmp`, `/var/tmp`, or `/run`. Pinned libfprint and fprintd checkouts are cloned
below `sources/` unless `GOODIX_MILAN_LIBFPRINT_SOURCE` or
`GOODIX_MILAN_FPRINTD_SOURCE` point at existing pristine checkouts.

Each build verifies the pinned patches, runs the libfprint and Milan test
suites, stages a `DESTDIR` payload, records its inventory, and publishes
`builds/current` only after verification. Building never changes the system.

Preflight refuses to continue when a destination is owned by a package or
already exists without being recorded by a previous source install. Remove the
conflicting packages first; it never overwrites them.

## Update And Remove

Update by checking out the wanted revision and repeating build and install. The
installer replaces its recorded files, removes files that the new build no
longer ships, and keeps a modified `/etc/fprintd.conf`. Ownership is recorded
before files are copied, so an interrupted install can simply be rerun.

```sh
./uninstall.sh
./scripts/status-milan-stack-local.sh --absent
```

Uninstall deletes recorded files whose contents are unchanged. Files you have
modified are listed and kept, together with the inventory; delete them and rerun
to finish. Shared directories, print state, and the PSK are never removed, and
no packages are reinstalled.

Both operations runtime-mask `fprintd.service` while files change so D-Bus
cannot activate a half-replaced pair, then unmask it. If an install or removal
fails part-way, fprintd stays masked and the script prints what to fix; rerun
it afterwards. A reboot also clears the runtime mask.
