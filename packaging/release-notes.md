## Check your sensor first

List Goodix USB devices with `lsusb` (from `usbutils`):

```sh
lsusb -d 27c6:
```

| Result | What to do |
| --- | --- |
| `27c6:5335`, `27c6:5385` or `27c6:5395` | Supported by this release; choose a download below. Hardware validation currently covers the Dell XPS 13 9305 with `27c6:5335`. |
| Another `27c6:` Goodix ID | Not supported by this release yet. Run the detector below and report the result. |
| No output | No Goodix USB fingerprint sensor was found; this release does not support your reader. |

The detector talks to the sensor directly, so it needs root and a C compiler with the GUsb development files (`gcc pkg-config libgusb-dev` on Debian and Ubuntu, `gcc pkgconf-pkg-config libgusb-devel` on Fedora, `gcc pkgconf libgusb` on Arch):

```sh
git clone --branch @TAG@ https://github.com/seaweeduk/goodix53x5-libfprint
cd goodix53x5-libfprint
sudo ./scripts/goodix53x5-detect.sh
```

If it reports `COMPATIBLE CANDIDATE`, [open a compatibility issue](https://github.com/seaweeduk/goodix53x5-libfprint/issues/new) with its output, your laptop model and your Linux distribution.

## Which download do I need?

| Distribution | Install |
| --- | --- |
| Ubuntu 22.04 / 24.04 / 26.04, Debian 12 / 13 | Download both `.deb` files for your release, then `sudo apt install ./libfprint-goodix53x5_*.deb ./fprintd-goodix53x5_*.deb` |
| Fedora 43 / 44 | Download both `.rpm` files for your release, then `sudo dnf install ./libfprint-goodix53x5-*.rpm ./fprintd-goodix53x5-*.rpm` |
| Arch and other distributions | Build this release from source (below) |

The packages replace the distribution's fprintd and its PAM module. Fingerprint login starts disabled. See the [installation guide](https://github.com/seaweeduk/goodix53x5-libfprint/blob/@TAG@/README.md#install) for enabling it and for removal.

### Arch and other distributions

Build and install this release with the source installer:

```sh
git clone --branch @TAG@ https://github.com/seaweeduk/goodix53x5-libfprint
cd goodix53x5-libfprint
./install.sh
```

Alternatively, extract `goodix53x5-libfprint-@VERSION@.tar.xz` from the assets below and run `./install.sh` inside it; it contains the pinned libfprint and fprintd sources and builds offline. To update later, repeat the same steps with the newer release.

GitHub's automatically generated "Source code" archives lack the pinned sources and release version; do not use them with `./install.sh`. The AUR's `libfprint-goodix53x5` package is built from [AndyHazz/goodix53x5-libfprint](https://github.com/AndyHazz/goodix53x5-libfprint), not from this release.

### Verify downloads

`sha256sum --check --ignore-missing SHA256SUMS` or `gh attestation verify FILE --repo seaweeduk/goodix53x5-libfprint`.
