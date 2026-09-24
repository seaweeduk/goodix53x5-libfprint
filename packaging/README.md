# Packaging

Distribution packages are built for Ubuntu 22.04, 24.04 and 26.04, Debian 12
and 13, and Fedora 43 and 44 on x86-64. Other distributions, including Arch,
use the source installation described in the main README.

## Packages

Both formats build two packages from the `goodix53x5-libfprint` source:

- `libfprint-goodix53x5` installs libfprint with only the `goodix53x5` driver
  in the private directory `<libdir>/libfprint-goodix53x5`. It has no
  development files and never replaces the distribution's libfprint.
- `fprintd-goodix53x5` installs the patched fprintd at the distribution's
  normal paths: daemon, `fprintd-*` commands, PAM module, systemd unit, D-Bus
  and polkit files, and the Milan udev rule. The daemon loads the private
  libfprint through its `RUNPATH`. It depends on the exact matching
  `libfprint-goodix53x5`.

`fprintd-goodix53x5` replaces the distribution's fprintd packages, which the
package manager removes during installation:

| | Debian and Ubuntu | Fedora |
| --- | --- | --- |
| Satisfies dependencies on | `fprintd`, `libpam-fprintd` (`Provides`, version 1.94.5) | `fprintd`, `fprintd-pam`, `fprintd-devel` (`Provides`, version 1.94.5) |
| Removes | `fprintd`, `libpam-fprintd` (`Conflicts` and `Replaces`) | `fprintd`, `fprintd-pam`, `fprintd-devel` below 1.95 (`Obsoletes`) |
| Fingerprint login | Ships the distribution's `pam-auth-update` profile, disabled by default and unregistered on removal | Uses authselect's `with-fingerprint` feature, disabled on removal |

The distribution's libfprint stays installed but unused: only the fprintd
daemon links libfprint, and it resolves the private copy first. The packaged
fprintd needs libfprint symbols that only the patched build provides, so it
cannot start against the distribution's library by mistake.

The package scripts refuse to install over a source installation (an existing
`/usr/share/goodix53x5-milan/inventory.json`, or the older
`/opt/goodix53x5-milan` layout and its fprintd drop-in). On installation they
reload udev, re-apply the Milan rule to an attached sensor, and restart fprintd
so the sensor is opened immediately. Print state in `/var/lib/fprint` is never
removed.

## Build Pipeline

1. `make-source-archive.sh VERSION DIR` archives `HEAD` and fetches the pinned
   libfprint and fprintd revisions as shallow checkouts under `sources/`. The
   archive records its version, revision and commit time in `release.env`.
   Tracked files must be committed.
2. `build-package.sh [--install-deps] ARCHIVE DIR` extracts the archive and
   builds the host distribution's packages with `dpkg-buildpackage` or
   `rpmbuild`. `--install-deps` installs the build dependencies as root.
   Package changelogs are dated from the release commit.
3. Both `debian/rules` and the RPM spec run
   `scripts/build-milan-stack-local.sh --package-root DIR`. This is the same
   builder as the source installation: it verifies the pinned sources and patch
   digests, applies the overlays, runs the libfprint and Milan test suites, and
   stages a release payload. The package layout only moves libfprint into its
   private directory, links the daemon to it, and drops libfprint's
   development files.

Pull requests that change packaging, patches or the stack builder build every
package through `.github/workflows/packages.yml`.

### Building Locally

With podman or docker, build any target from a committed checkout:

```sh
packaging/make-source-archive.sh 0.0.0~dev dist
podman run --rm -v "$PWD:/src" -w /src docker.io/library/ubuntu:24.04 \
  packaging/build-package.sh --install-deps dist/goodix53x5-libfprint-0.0.0~dev.tar.xz dist/out
```

Replace the image with `ubuntu:22.04`, `ubuntu:26.04`, `debian:12`,
`debian:13`, `fedora:43` or `fedora:44`. Build output stays under
`.build/packages`.
