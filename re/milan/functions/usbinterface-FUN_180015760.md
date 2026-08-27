# usbinterface.dll FUN_180015760

## Identity

- Address: `0x180015760`
- Logged name: `MilanHV_RetryCaptureIMG`
- Role: service the vendor-private live-image retry request.

## Findings

- If context state `+0x200` equals `10`, it writes retry-success byte `+4` as
  zero and returns zero without reading FDT or changing sensor mode.
- Otherwise it reads 12-word TX-on and TX-off FDT samples through callback
  `+0x160`, in that order with arguments one and zero. Each word is shifted
  right once before comparison.
- `FUN_180014c98` returns zero when any area has
  `abs((tx_on[i] >> 1) - (tx_off[i] >> 1)) > context->delta_fdt_31c`. The
  instruction uses signed greater-than after zero-extending the 16-bit
  threshold; the normalized absolute difference is at most `32767`.
- A zero predicate result captures one image through callback `+0x158` with
  TX-on, HV, DAC-high at `+0x312`, adjust-DAC, finger-image, and capture-mode
  arguments all set to one. Callback return zero sets retry-success `+4` to one,
  calls sensor-mode callback `+0xb0(0)`, and copies the image to output `+5`.
- A nonzero predicate result does not read an image. It leaves retry-success
  zero and calls sensor-mode callback `+0xb0(1)`.
- Either FDT callback returning `-1`, image callback returning `-1`, or image
  allocation failure leaves retry-success zero and calls `+0xb0(0)`. The first
  failing callback determines the function return; null context or output
  returns `-1`.
- Image size is the low 16 bits of `rows_u8_1f0 * columns_u8_1f1 * 2`. For
  profile 9/type 12 this is `19008` bytes.
- It neither clears base validity bytes `+0x232/+0x237` nor calls
  `MilanHV_update_allbase`; successful and rejected retries leave the HAL
  context unchanged.
- `FUN_1800162ac` installs the function as HAL retry callback slot `+0x1c0` at
  `0x1800163fb..0x180016402`; it has no static direct caller because dispatch is
  indirect.

## Request Boundary

- `FUN_180022afc` (`OnRetryCaptureIMG`) owns IOCTL `0x442010`. It requires a
  `0x7600`-byte output, clears the complete caller-provided buffer, writes
  structure size `0x7600` at `+0`, and dispatches device action `0x10`.
- Device action `0x10` invokes HAL slot `+0x1c0` when both the callback and
  output are non-null. The callback return is not propagated; the request
  completes successfully with the structure size as its information value.
- The meaningful retry structure is therefore size `+0`, success byte `+4`,
  and image bytes from `+5`; unused trailing bytes remain zero.

## Operation Reachability

`GoodixEngineAdapter.dll` 2.0.310.900 does not issue IOCTL `0x442010` from an
ordinary WBF operation. Its four `_RetryCaptureIMG` log literals have no code
references, no retry wrapper is emitted between the verify and identify entry
points, and its fixed `DeviceIoControl` call sites do not select this request.
The engine test-control path can forward an arbitrary caller-supplied IOCTL,
but that is not an identify, verify, or enrollment producer. The device INF
selects Windows' built-in `WinBioSensorAdapter.dll`; its ordinary WBDI capture
uses standard IOCTL `0x440014`, which dispatches `OnCaptureData`, not the
vendor-private retry IOCTL. Within `usbinterface.dll`, `OnRetryCaptureIMG` is
the only caller that selects device action `0x10`.

## Error-Recovery Consequence

A retry-capture error does not invalidate the retained calibration base. Base
reacquisition is reserved for the initial action-`0x0c` path and the FDT drift
handlers. Ordinary capture-failure recovery does not issue this request.
