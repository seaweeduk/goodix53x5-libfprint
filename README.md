# Goodix HTK32 (27c6:5335 / 27c6:5385 / 27c6:5395) libfprint Driver

A libfprint driver for the Goodix HTK32 fingerprint sensor found in the **Dell XPS 13 9305**, the **Dell XPS 13 7390**, the **Dell XPS 15 9570** and possibly other laptops using the `27c6:5335`, `27c6:5385` or `27c6:5395` USB device.

## Hardware

- **Vendor ID:** `0x27c6`
- **Product IDs:** `0x5335`, `0x5385`, `0x5395`
- **Sensor:** 108 x 88 pixels, capacitive press-type
- **Known devices:** Dell XPS 13 9305, Dell XPS 13 7390 2-in-1, Dell XPS 15 9570

Check if you have this sensor:
```
lsusb | grep -E '27c6:(5335|5385|5395)'
```

## How It Works

The sensor provides raw 12-bit capacitive images encrypted with a TLS-like protocol (GTLS). The driver:

1. Initializes the sensor: PSK exchange, GTLS handshake, config upload, FDT calibration
2. Detects finger placement via FDT (Finger Detection Threshold) events
3. Captures and decrypts the fingerprint image
4. Matches using **SIGFM** (SIFT-based fingerprint matching via OpenCV)

Fingerprint matching uses SIFT keypoints with CLAHE preprocessing, Lowe's ratio test, and pairwise geometric verification. This approach works well with the small 108x88 sensor where traditional minutiae-based methods struggle.

## Dependencies

- **libfprint** source tree (tested with v1.94.10)
- **OpenCV 4** (`opencv_core`, `opencv_features2d`, `opencv_flann`, `opencv_imgproc`, `opencv_calib3d`)
- **OpenSSL 3.0+**
- Standard libfprint build dependencies (meson, ninja, glib, libgusb, etc.)

### Installing OpenCV

**Arch Linux:**
```
sudo pacman -S opencv
```

**Fedora:**
```
sudo dnf install opencv opencv-devel
```

**Ubuntu/Debian:**
```
sudo apt install libopencv-dev
```

## Important: udev Rule

The sensor exposes a CDC (Communications Device Class) descriptor that causes the Linux `cdc_acm` kernel driver to claim it as a modem device, blocking libfprint. You **must** install the included udev rule:

```bash
sudo cp 91-goodix-fingerprint.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

This automatically unbinds `cdc_acm` when it tries to attach to this device. Without this rule, the driver will fail with "Resource busy" errors after every reboot.

## Installation

### Arch Linux (AUR)

```bash
yay -S libfprint-goodix53x5
sudo pacman -S fprintd
sudo systemctl restart fprintd
```

This builds a patched libfprint with the driver and udev rule included. No manual steps needed.

### Other Distros: Quick Start

```bash
# Clone libfprint
git clone https://gitlab.freedesktop.org/libfprint/libfprint.git
cd libfprint

# Apply this driver (also installs the udev rule)
/path/to/goodix53x5-driver/install.sh .

# The install script will print manual meson.build edits needed.
# Apply those edits, then:

meson setup builddir
cd builddir
ninja
sudo ninja install
sudo systemctl restart fprintd
```

### Manual Integration

1. Copy `drivers/goodix53x5/` into `libfprint/libfprint/drivers/goodix53x5/`
2. Copy `sigfm/` into `libfprint/libfprint/sigfm/`
3. Edit `libfprint/libfprint/meson.build`:
   - Add to the `driver_sources` dictionary:
     ```meson
     'goodix53x5' :
         [ 'drivers/goodix53x5/goodix53x5.c', 'drivers/goodix53x5/goodix53x5-proto.c', 'drivers/goodix53x5/goodix53x5-crypto.c', 'drivers/goodix53x5/goodix53x5-device.c' ],
     ```
   - Add SIGFM static library build (before `libfprint_drivers`):
     ```meson
     opencv_inc = include_directories('/usr/include/opencv4')
     opencv_core = cc.find_library('opencv_core')
     opencv_features2d = cc.find_library('opencv_features2d')
     opencv_flann = cc.find_library('opencv_flann')
     opencv_imgproc = cc.find_library('opencv_imgproc')
     opencv_calib3d = cc.find_library('opencv_calib3d')
     opencv_dep = declare_dependency(
         include_directories: opencv_inc,
         dependencies: [opencv_core, opencv_features2d, opencv_flann, opencv_imgproc, opencv_calib3d],
     )
     libsigfm = static_library('sigfm',
         'sigfm/sigfm.cpp',
         dependencies: [opencv_dep],
         cpp_args: ['-std=c++17'],
         install: false)
     ```
   - Add `libsigfm` to `link_with` for both `libfprint_drivers` and the main `libfprint` library
   - Add `opencv_dep` to the main library `dependencies`
4. Edit root `meson.build`:
   - Add `'goodix53x5'` to the default drivers list
   - Add `'goodix53x5' : [ 'openssl' ]` to `driver_helpers`
5. Reconfigure and build

## Enrollment and Verification

After installation, use your desktop environment's fingerprint settings (GNOME, KDE, etc.) or the command line:

```bash
# Enroll a finger (8 samples required)
fprintd-enroll

# Verify
fprintd-verify
```

## Troubleshooting

### `No devices available` (`net.reactivated.Fprint.Error.NoSuchDevice`)

The most common cause is that the udev rule has not taken effect, so `cdc_acm`
is still holding the device. **A full reboot is required**: reloading udev
rules or restarting `fprintd` alone is usually not enough, because `cdc_acm`
binds at USB enumeration time.

1. Confirm the device is present:
   ```bash
   lsusb | grep -E '27c6:(5335|5385|5395)'
   ```
2. Confirm the udev rule is installed and reload it:
   ```bash
   ls /etc/udev/rules.d/91-goodix-fingerprint.rules
   sudo udevadm control --reload-rules
   ```
3. **Reboot.**
4. Check that `cdc_acm` is *not* bound to the sensor (no `ttyACM*` should map to it):
   ```bash
   dmesg | grep -i cdc_acm
   ```

### `GTLS identity verification failed`

Seen on the first open after boot: the GTLS handshake occasionally fails on
the first attempt. Simply **run the enroll/verify command again**; it usually
succeeds on the second try. Some users have also reported that
`sudo systemctl restart rsyslog` followed by a retry clears it.

### `Device 27c6:XXXX is already open`

Another driver or process is already holding the device. The usual culprit is
a conflicting **TOD (Touch OEM Driver)** package shipping a different Goodix
driver. Remove it so it stops claiming the device:

```bash
# Debian/Ubuntu/Kali: check for and remove the conflicting TOD driver
dpkg -l | grep goodix
sudo apt remove libfprint-2-tod1-goodix
```

Then reboot and try again. If a stale `fprintd` is holding the device, you can
also try `sudo systemctl restart fprintd`.

### Capturing debug logs

To gather logs for a bug report, stop the system `fprintd` and run it in the
foreground with debug logging:

```bash
sudo systemctl stop fprintd
sudo G_MESSAGES_DEBUG=all /usr/libexec/fprintd -t
# in another terminal:
fprintd-enroll
```

(The `fprintd` binary path may be `/usr/lib/fprintd/fprintd` on some distros.)

## Technical Notes

- **SIGFM matching** uses OpenCV SIFT features with CLAHE contrast enhancement. Candidate correspondences (Lowe's ratio test) are confirmed by RANSAC: a single rigid+scale transform (`estimateAffinePartial2D`) is fitted between probe and enrolled keypoints, and the **inlier count** is the match score. A probe must clear a minimum-keypoint quality gate, then the sum of inliers across the 8 stored samples must clear a threshold (see `goodix53x5.h`). Using a single global transform, rather than counting locally-consistent pairs, strongly reduces false accepts from adjacent fingers compared to the earlier metric.
- **8 enrollment samples** are stored as raw 108x88 grayscale images. During verification, SIFT features are extracted from each stored sample and compared with the live capture.
- **Image preprocessing** removes horizontal banding and vertical striping via row/column mean subtraction, then normalizes to 8-bit.
- Thermal throttling is disabled (`temp_hot_seconds = -1`) since the small sensor generates negligible heat.

## ⚠️ Security limitations

This is a small (108x88 px) press sensor, and SIFT-based matching **cannot fully distinguish your enrolled finger from other fingers** on it. On-hardware testing (see [issue #3](https://github.com/AndyHazz/goodix53x5-libfprint/issues/3)) found the genuine and impostor match-score distributions overlap: with the default thresholds, roughly 1 in 15 impostor presses can still be accepted, and your own finger is accepted on roughly 1 in 3 presses (so expect to press 2-3 times).

The RANSAC matcher here is a **large improvement** over the previous behaviour (which accepted almost any similar finger) but is **not a security guarantee**. Treat this sensor as **convenience-grade**, not as a sole factor for protecting anything sensitive. Tune `GOODIX_SIGFM_SUM_MIN` in `goodix53x5.h` higher (e.g. 42) to reduce false accepts at the cost of more retries, or lower for fewer retries at higher risk.

A minutiae-based matcher (libfprint's NBIS) was evaluated as a more discriminative alternative but is not viable here: `mindtct` extracts only about 5-7 minutiae from genuine captures on this sensor and bozorth3 needs roughly 12+, too few to match reliably. The small contact area is the fundamental limit.

## File Structure

```
91-goodix-fingerprint.rules - udev rule to unbind cdc_acm from the sensor

drivers/goodix53x5/
  goodix53x5.h           - Header: defines, structs, function declarations
  goodix53x5.c           - Main driver: SSMs for open, enroll, verify, identify
  goodix53x5-device.c    - Device helpers: OTP, config, FDT, image processing
  goodix53x5-proto.c     - USB protocol: message building, reassembly, parsing
  goodix53x5-crypto.c    - Crypto: GTLS, AES, HMAC, PSK, GEA decryption

sigfm/
  sigfm.hpp              - SIGFM C API header
  sigfm.cpp              - SIFT feature extraction and matching (with CLAHE)
  binary.hpp             - Binary serialization for print storage
  img-info.hpp           - SigfmImgInfo struct (keypoints + descriptors)
```

## Credits

- SIGFM matching library from [goodix-fp-linux-dev/sigfm](https://github.com/goodix-fp-linux-dev/sigfm), by Matthieu Charette, Natasha England-Elbro, and Timur Mangliev
- Protocol reverse-engineering from [goodix-fp-linux-dev](https://github.com/goodix-fp-linux-dev)

## License

LGPL-2.1-or-later (same as libfprint)
