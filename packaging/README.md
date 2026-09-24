# Packaging And Releases

Releases are GitHub releases built by `.github/workflows/release.yml`. Each one
carries:

| Asset | Contents |
| --- | --- |
| `goodix53x5-libfprint-VERSION.tar.xz` | Complete source: this repository plus the pinned libfprint and fprintd checkouts |
| `libfprint-goodix53x5_VERSION-1~DISTRO_amd64.deb`, `fprintd-goodix53x5_…` | Ubuntu 22.04, 24.04 and 26.04, Debian 12 and 13 |
| `libfprint-goodix53x5-VERSION-1.fcNN.x86_64.rpm`, `fprintd-goodix53x5-…` | Fedora 43 and 44 |
| `SHA256SUMS` | Checksums of every asset, also covered by GitHub build provenance attestations |

Arch and other distributions use the source installation. The release notes
tell those users to clone the release tag or extract the release source archive
and run `./install.sh`, and not to use GitHub's automatically generated source
archives, which lack the pinned sources and `release.env`.

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

## Releasing

Releases are made from `main`:

```sh
packaging/release.sh patch        # or minor, major, or an exact version such as 1.0.0
```

This starts the Release workflow (also available from the Actions tab). It:

1. selects the version from the latest `vX.Y.Z` tag, refusing anything that is
   not newer; the first release is `1.0.0`;
2. builds the source archive and all seven targets;
3. writes `SHA256SUMS` and build provenance attestations;
4. creates a **draft** release targeting the built commit, with the install
   instructions from `packaging/release-notes.md` (filled in with the tag) and
   notes generated from the pull requests merged since the previous release.

Review the draft on the releases page, edit the notes (add highlights and any
re-enrollment warnings), and publish it. Publishing creates the tag; nothing is
public before then. Package changelogs link to the release notes rather than
duplicating them.

### Versions

The tag is the only version source; no file is bumped.

- **Patch**: fixes and packaging-only changes.
- **Minor**: new behaviour or supported sensors, and compatible updates of the
  pinned libfprint or fprintd.
- **Major**: changes that require re-enrollment or break compatibility with the
  distribution's fprintd clients.

Package revisions stay at `-1`; a packaging fix is a new patch release. Source
installations record the version from `git describe` or `release.env` in
`/usr/share/goodix53x5-milan/build.env`.

### Release Notes

Notes are grouped by pull request label (`.github/release.yml`):

| Label | Section |
| --- | --- |
| `feature` | New features |
| `fix` | Fixes |
| `hardware` | Hardware support |
| `packaging` | Packaging and releases |
| `skip-notes` | Left out of the notes |

Unlabelled pull requests appear under "Other changes". Create the labels once:

```sh
for label in feature fix hardware packaging skip-notes; do
  GH_REPO=seaweeduk/goodix53x5-libfprint gh label create "$label"
done
```
