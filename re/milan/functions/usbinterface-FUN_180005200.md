# usbinterface.dll FUN_180005200

## Identity And Contract

- Address: `0x180005200`; logged name: `Milan_GetDacVal`.
- Installed at HAL slot `+0xe8` by `FUN_18000450c`; the factory
  `Test_OpenShortUpdateDAC` owner `0x18001f520` invokes that slot for a nonzero
  byte argument. It is not the ordinary live-read adjustment callback.
- Input: byte mode. Requests sensor mode 7 with timeout 200, ignoring its
  result, then reads register `0x220` through `FUN_1800180b0` into HAL word
  `+0x22c` and byte-swaps that word through `FUN_180009b48(2, ...)`.
- When selected OTP temperature global `DAT_1800606fa != 0`, mode zero writes
  word `+0x22e = 0x28`, mode one writes `0x1a`, and other modes preserve it.
  Zero temperature preserves the word for all modes.
- Returns the register-read status. The word conversion and conditional delta
  write are not gated on read success.
- This helper neither loads a saved base nor acquires a reference image. It
  does not publish live DAC word `+0x312`, low/reference DAC word `+0x310`,
  image-valid byte `+0x237`, or the one-shot setup marker `+0x236`.
- The package's `HVDacAdjustSwitch` compiled default is `0`; configuration
  loading belongs to [FUN_180009c6c](usbinterface-FUN_180009c6c.md).

Saved-base loading is `FUN_18000d24c`, described in
[the initialization owner](usbinterface-FUN_180020970.md). Fresh image-base
acquisition is [FUN_180015c60](usbinterface-FUN_180015c60.md); dynamic live
DAC adjustment is [FUN_180007c84](usbinterface-FUN_180007c84.md).

No Linux factory `Test_OpenShortUpdateDAC` entry point maps this callback.
The ordinary current-DAC update owner is
`drivers/goodix53x5/device/calibration.c:goodix_device_adjust_dac`, mapped to
`0x180007c84`, not this register-read helper.
