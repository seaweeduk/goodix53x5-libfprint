# Windows Dual-Boot PSK Migration

Windows and this Linux driver use a GTLS pre-shared key to protect traffic with
the fingerprint sensor. The key is independent of the fingerprint templates
stored by each operating system, so migration does not require re-enrollment.

This is an optional, one-time migration. Without
`/var/lib/fprint/goodix53x5.psk`, the driver retains its normal behavior and
automatically provisions the all-zero Linux PSK when necessary. When that file
exists, the driver uses the imported key and refuses to overwrite a sensor that
does not match it.

> [!WARNING]
> The PSK grants access to encrypted sensor traffic. Keep it private, never
> commit it, and delete transfer copies after installation.

## How It Works

The fingerprint reader has persistent memory shared by both operating systems.
When Windows Hello starts after Linux has used the all-zero key, the official
Goodix driver automatically restores or creates its own key in the reader.

Some Windows drivers store a readable key on the sensor; others store an
encrypted copy. The sensor's USB ID alone does not tell you which method will
work. Both export methods verify the key against the sensor's active-key hash
before saving it. Once imported, the Linux driver uses the same key and skips
PSK provisioning.

## Requirements

- A Goodix USB fingerprint sensor using the same PSK production records.
- The updated driver installed before beginning migration.
- For the Linux extractor: `cc`, `pkg-config`, and GUsb development files.
  These are already driver build dependencies.
- Administrator access on Linux and password or PIN login while fingerprint
  authentication is temporarily disabled.

Start with the [Linux extractor](#linux-extractor) for any 53x5 sensor. It does
not restrict product IDs and saves a key only if it matches the sensor's active
hash. It has been tested with `27c6:5335` and Windows driver `2.0.310.900`, but
that is a confirmed setup, not a compatibility limit.

The [Windows helper](#windows-helper) is an alternative for an encrypted key.
It currently supports only `27c6:5395` with Dell KP8ND driver `2.1.32.800`,
[confirmed working on a Dell XPS 15 9570][5395-confirmed]. It checks the exact
driver files and rejects other versions. Check the version in Windows Device
Manager, not the Dell download's package version.

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
could replace the Windows key before you import it. Existing Linux enrollments
are not deleted.

## 2. Export the Windows key

Try the Linux extractor first, or go straight to the Windows helper if you
have its supported driver. Once you have a key file, continue to step 3.

### Linux extractor

Boot Windows and perform a Windows Hello fingerprint operation. This gives the
Goodix driver an opportunity to detect the previous Linux key and restore or
replace it. Use a PIN or password first if fingerprint sign-in is temporarily
unavailable, then reboot into Linux.

Keep `fprintd` masked. From the repository checkout, run:

```sh
sudo ./scripts/extract-windows-psk.sh ./windows.psk
```

The dumper verifies that `fprintd` is masked and inactive, compiles a standalone
GUsb reader, reads `B001` and `B003`, and writes the key only when their hashes
match. It makes no persistent sensor writes and does not start GTLS.

If it reports `B001 is not the active plaintext PSK`, it has not exported a
key. The Windows driver may store an encrypted copy; use the Windows helper
below if it supports your setup.

### Windows helper

This method needs 64-bit Windows PowerShell 5.1 as administrator, SGX enabled
in firmware, and working Intel SGX Platform Software. No manual compilation
or extra PowerShell modules are needed.

1. Boot Windows. Download and extract the [Windows key export ZIP][5395-zip].
   Keep `Read-Goodix5395WindowsPsk.ps1` and `Goodix5395Sgx.cs` together.
2. Open **Windows PowerShell as Administrator** in the extracted folder and run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Read-Goodix5395WindowsPsk.ps1 -Action Start
   ```

3. Restart Windows and sign in using your fingerprint. Use a PIN or password
   first if needed, then complete a Windows Hello fingerprint operation.
4. Open elevated Windows PowerShell in the same folder again and run:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Read-Goodix5395WindowsPsk.ps1 -Action Finish
   ```

5. Copy `C:\ProgramData\Goodix5395KeyExport\windows.psk` somewhere you can
   access from Linux, then restart into Linux. Copy the file into the repository
   checkout for the next step.

The helper temporarily enables Goodix logging, uses the installed driver's
SGX component to recover the key, and restores the original logging settings.
Restarting into Linux unloads the diagnostic logger. It does not change the
sensor's key or firmware.

If it fails, share the error text and `driver.txt` from
`C:\ProgramData\Goodix5395KeyExport`, **not the key or logs**. To abandon a
capture, run the PowerShell command with `-Action Restore`, restart Windows,
and unmask `fprintd` when back on Linux. See the ZIP's README for details.

If neither method works for your setup, open an issue with your laptop model,
USB ID, Windows Device Manager driver version, and the error text. An untested
device is not necessarily incompatible; its Windows key storage needs checking.
Do not include keys or logs.

## 3. Import the key on Linux

Keep `fprintd` masked. From the repository checkout, run:

```sh
sudo ./scripts/install-windows-psk.sh ./windows.psk
```

The installer stores the key as 64 hexadecimal characters at
`/var/lib/fprint/goodix53x5.psk`, owned by root with mode `0600`. If a key was
installed previously, replacement requires `--replace` and preserves the old
file as `/var/lib/fprint/goodix53x5.psk.previous`.

## 4. Restore and verify

After the import succeeds:

```sh
sudo systemctl unmask fprintd.service
sudo systemctl restart fprintd.service
fprintd-verify
```

After verification succeeds, delete the extracted transfer copy:

```sh
rm ./windows.psk
```

For the Windows method, also delete the exported key and capture directory
`C:\ProgramData\Goodix5395KeyExport` from an elevated Windows session.

For final confirmation, boot Windows and unlock with Windows Hello, then boot
Linux and run `fprintd-verify` again. If both succeed, neither transition
replaced the shared key. A debug build (`./install.sh --debug`) also logs
`PSK hash matches, no need to write` to the `fprintd.service` journal when it
opens the sensor.

If an imported key does not match, the driver fails without changing the
sensor. Re-extract the current Windows key. Removing
`/var/lib/fprint/goodix53x5.psk` intentionally returns to Linux-only mode, where
the normal driver may replace the Windows key with its all-zero key.

[5395-zip]: https://github.com/user-attachments/files/34191822/goodix5395-windows-key-export.zip
[5395-confirmed]: https://github.com/seaweeduk/goodix53x5-libfprint/issues/335#issuecomment-6546898490
