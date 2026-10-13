# Windows Dual-Boot PSK Migration

Windows and this Linux driver use a pre-shared key to protect traffic with
the fingerprint sensor. The key is independent of the fingerprint templates
stored by each operating system, so migration does not require re-enrollment.

This is an optional, one-time migration. Without
`/var/lib/fprint/goodix53x5.psk`, the driver retains its normal behavior and
automatically provisions the all-zero Linux PSK when necessary, except on the
533c, which it never provisions. When that file exists, the driver uses the
imported key and refuses to overwrite a sensor that does not match it.

> [!WARNING]
> The PSK grants access to encrypted sensor traffic. Keep it private, never
> commit it, and delete transfer copies after installation.

## How It Works

The steps below do not apply to `27c6:533c`; it has its own
[procedure](#533c-windows-key-import).

The fingerprint reader has persistent memory shared by both operating systems.
When Windows Hello starts after Linux has used the all-zero key, the official
Goodix driver automatically restores or creates its own key in the reader.

Linux then reads that key directly from the reader over USB. It does not read
the Windows partition and runs no code in Windows. The extractor also reads the
reader's active-key hash and saves the key only when they match. Once installed,
the Linux driver uses the same key and skips PSK provisioning. If the key ever
does not match, Linux fails without replacing it.

## Requirements

- A Goodix USB fingerprint sensor using the same PSK production records.
- The updated driver installed before beginning migration.
- `cc`, `pkg-config`, and GUsb development files on Linux. These are already
  driver build dependencies.
- Administrator access on Linux and password or PIN login while fingerprint
  authentication is temporarily disabled.

Hardware validation currently covers `27c6:5335`. Its `B001` production value
is the raw 32-byte PSK. The extractor does not restrict Goodix product IDs: it
probes attached Goodix devices and writes a file only when `SHA256(B001)` equals
the active `B003` hash. Devices using another protocol or sealing format fail
without producing a key.

## 1. Install And Disable Linux Fingerprint Access

Install the release packages or a release build with `./install.sh`, or retain
debug capture support with a source installation:

```sh
./install.sh --debug
```

Before booting Windows, mask `fprintd`:

```sh
sudo systemctl mask --now fprintd.service
```

Masking persists across reboots and prevents desktop, PAM, or D-Bus activation
from opening the sensor when Linux next starts. Otherwise the normal driver
could replace the freshly established Windows key before the standalone dumper
reads it. Existing Linux enrollments are not deleted.

## 2. Establish The Windows Key

Boot Windows and perform a Windows Hello fingerprint operation. This gives the
Goodix driver an opportunity to detect the previous Linux key and restore or
replace it. Use a PIN or password first if fingerprint sign-in is temporarily
unavailable, then reboot into Linux.

## 3. Extract And Install The Key

Keep `fprintd` masked. From the repository checkout, run:

```sh
sudo ./scripts/extract-windows-psk.sh ./goodix53x5-windows.psk
sudo ./scripts/install-windows-psk.sh ./goodix53x5-windows.psk
```

The dumper verifies that `fprintd` is masked and inactive, compiles a standalone
GUsb reader, reads `B001` and `B003`, and writes the key only when their hashes
match. It makes no persistent sensor writes and does not start GTLS.

The installer stores the key as 64 hexadecimal characters at
`/var/lib/fprint/goodix53x5.psk`, owned by root with mode `0600`. If a key was
installed previously, replacement requires `--replace` and preserves the old
file as `/var/lib/fprint/goodix53x5.psk.previous`.

## 4. Restore And Verify

```sh
sudo systemctl unmask fprintd.service
sudo systemctl restart fprintd.service
fprintd-verify
```

After verification succeeds, delete the extracted transfer copy:

```sh
rm ./goodix53x5-windows.psk
```

For final confirmation, boot Windows and unlock with Windows Hello, then boot
Linux and run `fprintd-verify` again. If both succeed, neither transition
replaced the shared key. A debug build (`./install.sh --debug`) also logs
`PSK hash matches, no need to write` to the `fprintd.service` journal when it
opens the sensor.

If an imported key does not match, the driver fails without changing the
sensor. Re-extract the current Windows key. Removing
`/var/lib/fprint/goodix53x5.psk` intentionally returns to Linux-only mode, where
the normal driver may replace the Windows key with its all-zero key.

## 533c Windows Key Import

> [!WARNING]
> This procedure for the Dell XPS 13 9310 (`27c6:533c`) has not been tested on
> hardware or on Windows yet. Please report how it goes.

On the 533c, Dell's Windows driver sets its own random key on the sensor and
keeps a copy that only that Windows installation can unseal. This driver never
changes the key on a 533c, so Linux needs a copy of the Windows key. The import
reads from the sensor but never writes to it. You need a USB stick or another
drive that both systems can read.

1. Install this driver, then stop `fprintd` and keep it from starting, so that
   nothing else uses the sensor during the import:

   ```sh
   sudo systemctl mask --now fprintd.service
   ```

   Boot Windows, restart it once, and sign in with your fingerprint. Do this
   even if Linux fingerprint already works. When Dell's driver finds a key
   other than its own, it sets its key on its first start and leaves the
   sensor in its bootloader, so Windows offers no fingerprint sign-in until
   the restart, when the driver installs its firmware. Then return to Linux. If Linux finds the sensor still in its
   bootloader, `sudo ./scripts/goodix53x5-detect.sh` says so: start Windows
   once more.
2. Save the sealed key to the shared drive:

   ```sh
   sudo ./scripts/extract-windows-psk.sh --533c /path/to/drive/goodix533c.dpapi
   ```

3. Copy `scripts/unprotect-windows-psk.ps1` to the drive and boot Windows. Open
   Windows PowerShell as administrator and run the following, with `E:`
   replaced by the drive letter:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File E:\unprotect-windows-psk.ps1 -BlobFile E:\goodix533c.dpapi -OutputFile E:\goodix533c.psk
   ```

   If it reports that it could not unprotect the key, run it in the Windows
   installation that last used the sensor.
4. Boot Linux and install the key:

   ```sh
   sudo ./scripts/install-windows-psk.sh --533c /path/to/drive/goodix533c.psk
   ```

   The installer checks the key against the sensor and refuses one that does
   not match. Add `--replace` if a key is already installed.
5. Start `fprintd` again and check your fingerprint:

   ```sh
   sudo systemctl unmask fprintd.service
   sudo systemctl restart fprintd.service
   fprintd-verify
   ```

   Once it works, delete both files from the drive, then check that Windows
   Hello still works.

Removing `/var/lib/fprint/goodix53x5.psk` does not reset a 533c. The driver
refuses to open it again until the key is imported.

## 533c Reset Without Windows

If Windows has used the sensor and that installation is gone, its key can't be
recovered. Dell's own Linux driver can still reset the sensor: it writes its
all-zero key and reinstalls its firmware (`GF5288_GM168SEC_APP_13016`), after
which this driver opens it with no import. The steps below run Dell's driver
once in a Podman container, so nothing from Dell is installed on the system.

> [!WARNING]
> This rewrites the sensor's key and firmware. It worked on one Dell XPS 13
> 9310 five times, from the Windows key with Windows' firmware and from the
> bootloader state. Keep the laptop on mains power and don't suspend it while
> it runs.

`sudo ./scripts/goodix53x5-detect.sh` tells you whether you need this. Run it
before and after.

1. Stop `fprintd` and keep it from starting, as for the import:

   ```sh
   sudo systemctl mask --now fprintd.service
   ```

2. In an empty directory, download Dell's package and check it. Dell's archive
   serves it over plain HTTP, so the checksum is what verifies it:

   ```sh
   curl -LO 'http://dell.archive.canonical.com/updates/pool/public/libf/libfprint-2-tod1-goodix/libfprint-2-tod1-goodix_0.0.6-0ubuntu1~somerville1_amd64.deb'
   echo 'b921e155fd35b06341a453b9c6f6c4c7101f199aee314403826c517156a4b038  libfprint-2-tod1-goodix_0.0.6-0ubuntu1~somerville1_amd64.deb' | sha256sum -c
   ```

3. Next to it, create `Containerfile`:

   ```dockerfile
   FROM docker.io/library/ubuntu:24.04
   RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
         libfprint-2-tod1 gir1.2-fprint-2.0 python3-gi && rm -rf /var/lib/apt/lists/*
   COPY *.deb /tmp/
   RUN dpkg -i /tmp/*.deb || (apt-get update && apt-get -f install -y)
   ```

   and `open.py`, which opens the sensor once with Dell's driver:

   ```python
   import gi
   gi.require_version("FPrint", "2.0")
   from gi.repository import FPrint
   for d in FPrint.Context().get_devices():
       print("device:", d.get_driver(), d.get_name())
       d.open_sync()
       print("opened")
       d.close_sync()
       print("closed")
   ```

4. Build the image, give your user access to the sensor until the next
   reboot, and run it:

   ```sh
   podman build -t goodix-tod-reset .
   sudo setfacl -m "u:$USER:rw" "$(lsusb -d 27c6:533c | awk '{printf "/dev/bus/usb/%s/%s", $2, substr($4, 1, 3)}')"
   podman run --rm -v /dev/bus/usb:/dev/bus/usb -v "$PWD/open.py:/open.py:ro" \
     --security-opt label=disable --userns keep-id goodix-tod-reset python3 /open.py
   ```

   Dell's driver resets the sensor while detecting it, within a few seconds,
   and the sensor then restarts, so the script usually lists no device. On a
   sensor that already has the all-zero key it prints the device, `opened` and
   `closed` instead. Either way, check the result in the next step.

5. Check the result. The detector should report `MATCH`:

   ```sh
   sudo ./scripts/goodix53x5-detect.sh
   ```

   If it reports another key imported at `/var/lib/fprint/goodix53x5.psk`,
   remove that file. Then start `fprintd` again and enroll:

   ```sh
   sudo systemctl unmask fprintd.service
   sudo systemctl restart fprintd.service
   fprintd-enroll
   ```

   The image can be removed with `podman rmi goodix-tod-reset`.

On Arch, the AUR packages the same driver as `libfprint-2-tod1-xps9300-bin`,
on top of `libfprint-tod`. Its library is identical to the one in Dell's
package apart from stripped debug notes, and it opens the 533c when built and
run in an Arch container. Installed on the host, though, it replaces this
driver's packages and the distribution's libfprint until it is removed again,
so the Podman steps above are the simpler route.

If you install Windows again later, Dell's Windows driver sets its own key
again on its first start. The sensor then needs the
[import](#533c-windows-key-import), or this reset if Windows is gone again.
