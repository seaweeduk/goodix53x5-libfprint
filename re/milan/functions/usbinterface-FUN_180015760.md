# usbinterface.dll FUN_180015760

## Identity

- Address: `0x180015760`
- Logged name: `MilanHV_RetryCaptureIMG`
- Role: retry one live HV image after an unsuccessful capture request.

## Findings

- Reads TX-on and TX-off FDT bases and rejects the retry when they indicate a
  finger state unsuitable for recapture.
- On a usable state, captures one TX-on/HV live image with DAC-high and copies
  it to the retry output.
- FDT-read or image-read failures mark the retry output unsuccessful and change
  sensor mode through callback `+0xb0`.
- It neither clears base validity bytes `+0x232/+0x237` nor calls
  `MilanHV_update_allbase`.
- `FUN_1800162ac` installs the function as HAL retry callback slot `+0x1c0` at
  `0x1800163fb..0x180016402`; it has no static direct caller because dispatch is
  indirect.

## Error-Recovery Consequence

A retry-capture error does not invalidate the retained calibration base. Base
reacquisition is reserved for the initial action-`0x0c` path and the FDT drift
handlers.
