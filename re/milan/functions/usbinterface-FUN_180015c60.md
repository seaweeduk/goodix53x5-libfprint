# usbinterface.dll FUN_180015c60

## Identity

- DLL SHA-256: `619f1b708be2d724f4bbe3d08f656586ccb4153afa97a18f659fa8338515e07f`
- Address: `0x180015c60`
- Body: `0x180015c60..0x18001609a`
- Logged name: `MilanHV_update_allbase`
- Role: acquire and validate Milan HV-series FDT/image bases.

## Production Image-Base Sequence

For profile 9 / chip subtype `0x0c`, the function allocates two full-size
16-bit image buffers and performs this sequence:

1. Read FDT base with TX enabled at `0x180015d58`.
2. Capture one no-finger image at `0x180015d91` using low DAC from context
   `+0x310`, TX enabled, HV enabled, and the non-finger capture mode.
3. Read FDT base with TX disabled at `0x180015dad`.
4. Capture one no-finger image at `0x180015e4f` with the same low-DAC/HV/mode
   settings but TX disabled.
5. Validate the image pair through `FUN_180014ce8` at `0x180015e7c`, using
   threshold word `+0x314`.
6. Read FDT base with TX enabled again at `0x180015ed7` and validate it against
   the prior TX-off FDT base.
7. On success, copy only the first, TX-on image into retained context buffer
   `+0x248` at `0x180015f45..0x180015f52`; set image-base-valid byte `+0x237`
   at `0x180015f82`.

The TX-off image is validation input and is freed. It is not averaged with the
TX-on image and is not retained as the engine reference.

## Settings

- Dimensions: rows are context byte `+0x1f0` (`88`) and columns are `+0x1f1`
  (`108`). The validator receives them in that order.
- Image byte count: `rows * columns * 2` (`0x4a40`).
- DAC: context word `+0x310`, the OTP/default low DAC (`dac_l`; production
  metadata is `0xc6`).
- HV: enabled for both image captures. The boolean argument selects dynamic
  configuration byte `HVvalue` at config `+0x426`; the compiled byte at
  `0x18005e766` is `6`, so request byte 1 is `0x06` (HV disabled would use
  `0x10`).
- Finger mode: no-finger/base capture for both images.
- TX: first image enabled, second image disabled.
- Pair-validity threshold: context word `+0x314`; `FUN_180004a40` writes `200`
  in both OTP-validity branches.

## Independent Adversarial Audit

The hardware image function pointer at context `+0x158` is assigned
`FUN_1800055d0` at `0x18000454e..0x180004555`. Its effective capture ABI is:

```c
int read_image(uint16_t *output, bool tx_enable, bool hv_enable,
               uint16_t *dac, bool adjust_dac, bool is_finger,
               uint8_t unused_capture_mode);
```

`FUN_1800055d0` forwards these values to packet builder `FUN_1800074bc`.
At `0x18000753b..0x180007548`, `tx_enable=1` produces request byte 0 `0x01`
and `tx_enable=0` produces `0x81`. At `0x180007548..0x18000758b`, the separate
finger flag adds `0x40` and the separate HV flag selects `HVvalue` versus
`0x10`. The DAC is serialized little-endian at `0x18000755a..0x1800075cc`.
The function copies the sensor response into the caller's output at
`0x18000762a..0x180007651`; it does not allocate or retain that output.
The final caller argument is forwarded by `FUN_1800055d0` but is not consumed
by this packet builder.

The two calls in this function are therefore exactly:

```text
0x180015d69..0x180015d91:
  output=R14, tx=1, hv=1, dac=&context[0x310], adjust=0, finger=0, mode=0
  production request = 01 06 c6 00

0x180015e31..0x180015e4f:
  output=RSI, tx=0, hv=1, dac=&context[0x310], adjust=0, finger=0, mode=0
  production request = 81 06 c6 00
```

This polarity is independently fixed by `MilanHV_RetryCaptureIMG`: calls to
the FDT function with argument `1` and `0` have failure strings
`GetFDTBase failed with TX enable!!!!` at `0x18001581b` and
`GetFDTBase failed with TX disable!!!!` at `0x18001588f`. Normal live-image
calls at `0x180015297..0x1800152c5` instead use high DAC `+0x312`, dynamic DAC
adjustment `1`, and finger flag `1`; this rules out DAC, adjustment, HV, or
finger mode as the meaning of the bit that differs between the two base calls.

The image validator call at `0x180015e5e..0x180015e7c` passes arguments in
this exact order: rows (`+0x1f0`), columns (`+0x1f1`), first/TX-on buffer
`R14`, second/TX-off buffer `RSI`, threshold (`+0x314`). The helper is
read-only and computes `abs(first - second)`, so its result is mathematically
symmetric even though the call order is not ambiguous. The first FDT check at
`0x180015dcc..0x180015de2` similarly receives first TX-on FDT, then TX-off
FDT; the second check at `0x180015ee6..0x180015efa` receives the new TX-on FDT,
then the same TX-off FDT.

## Ownership Proof

- `R14` is the first temporary allocation (`0x180015d03..0x180015d0b`), and
  `RSI` is the second (`0x180015d17..0x180015d1f`).
- The persistent context `+0x248` destination is allocated by the HAL at
  `0x18001652c..0x18001653b` with `rows * columns * 2` bytes.
- The only successful retention copy here is destination `context->+0x248`,
  source `R14`, length `0x4a40` at `0x180015f45..0x180015f52`.
- Both temporaries are freed at `0x18001601f..0x18001602f`. The persistent
  `+0x248` allocation is instead freed by HAL shutdown at
  `0x18001620e..0x18001621f`.

## Falsification Result

The hypothesis "Windows retains TX-on and discards TX-off" survives all
attempted alternatives:

- Reversing the TX boolean conflicts with packet bytes `0x01/0x81`, the packet
  builder's `txEnable` log, Linux's independently matching request encoding,
  and the neighboring explicit TX-enable/TX-disable failure strings.
- Swapping capture outputs conflicts with the first call using `R14`, the
  second using `RSI`, and the final copy loading only `R14` into `RDX`.
- Treating validation as a transform conflicts with `FUN_180014ce8` being
  read-only and returning only a boolean.
- Treating the callback's live frame as the base conflicts with callback setup
  at `0x18000e629..0x18000e652`: argument 2 (`RDX`) is context `+0x248`, while
  argument 3 (`R8`) is live frame `+0x340`.

Captured Linux data supplies a useful counterexample to careless artifact
reasoning. Converting
`real-learning-profile9-production-20260723/enroll8/artifacts/setup-frame-u16le.bin`
back to raw12 PGM gives SHA-256
`2fa05f2613928c8ef88243530dd5303045e814b638b124e198eccd17b7d4de35`, exactly
the TX-off capture
`raw12-open-calib-dacl-tx-off-hv-on-nofinger-1-1780939735089-072685a6.pgm`.
Its paired TX-on capture is different (SHA-256
`05bb76cdf1f12a41237861d309bb1085789c8a2901c64c3b5848f907dc373f95`), while
their interior mean absolute difference is about `29.995`, below the Windows
threshold `200`. This proves those oracle fixtures intentionally inject a
Linux TX-off setup frame; it does not overturn the Windows handoff trace.

## Lifetime And Refresh

- One call captures exactly two image-base frames and retains one TX-on frame.
- The hardware-profile initializer `FUN_1800162ac` installs this function at
  HAL-context callback slot `+0x180` at `0x1800163a7..0x1800163ae`.
- Full device initialization in `FUN_180020970` issues device action `0x0c` at
  `0x180020d04`. `FUN_18000e1f0` loads callback slot `+0x180` at
  `0x18000e883` and invokes it at `0x18000e892`. This is the initial
  `MilanHV_update_allbase` call beyond the three static direct callers.
- `FUN_1800141f0` calls this routine at `0x180014257` when base-valid byte
  `+0x232` is clear, covering invalid-base setup.
- `FUN_180013da4` (`MilanHV_temperature_event`) clears `+0x232`, reruns this
  routine at `0x180013e01`, and on success sets one-shot refresh byte `+0x236`.
- `FUN_180014480` (`Reverse_Occure`) directly calls this routine at
  `0x1800144cd` when `+0x232 != 1`.
- Those are all three direct code callers reported by Ghidra. A fourth route is
  the callback-slot dispatch above.
- Success writes word `0x0101` at `+0x232` (therefore setting bytes `+0x232`
  and `+0x233`) at `0x180015f72`, then sets image-base-valid `+0x237` at
  `0x180015f82`.
- The function does not clear `+0x232` or `+0x237` on entry or on acquisition
  failure. Callers that require invalidation clear `+0x232` before calling.
- `FUN_18000f894` clears `+0x232` and `+0x237`, but its four direct callers are
  Milan F-series routines (`FUN_18000f9e0`, `FUN_1800100d0`, `FUN_1800102f8`,
  and `FUN_180010690`). No profile-9 HV call to it is proven.
- Normal probes reuse context `+0x248`; this function is not called per probe.
- UMDF D0 exit/entry does not clear these flags and does not free `+0x248`.
  Full hardware release reaches `FUN_1800160a0`, which frees `+0x248` but does
  not visibly clear `+0x232` or `+0x237`.

## Handoff

- Capture callback dispatchers load retained context `+0x248` as callback
  argument 2, including the call at `0x18000e63d..0x18000e652`.
- `CaptureFramedone` (`0x18001fb40`) serializes that argument as the engine's
  sample-carried setup/reference plane.
- `CaptureFramedone` copies callback argument 2 at
  `0x18001fcbc..0x18001fcd9`; `GoodixEngineAdapter.dll` then copies that plane
  from payload `+0xebf0` at `0x18001fc5d..0x18001fc86` and passes it to
  `_InitPreProcessor_Unify` at `0x18001fed6..0x18001fef2`. The exported
  `preprocessor_init` receives the same single frame through
  `FUN_180031d00 -> FUN_18002bfa0`, with its setup object built at
  `0x18002c256..0x18002c299` and the call at `0x18002c656..0x18002c65e`.

Confidence: very high for argument meanings, packet bytes, validator order,
ownership, retention, and preprocessing handoff. The only environment-dependent
item is a registry override of `HVvalue`; the installed package has no override,
so the compiled production value is `6`.

## Unresolved

- Whether UMDF unloads this DLL or otherwise zeroes its global HAL context
  between `ReleaseHardware` and a later attach. In-process re-enable allocates a
  new `+0x248` buffer and performs the initial action-`0x0c` refresh, but the
  static code does not itself clear the old validity bytes before that refresh.
- The return from initial action `0x0c` is ignored by `FUN_180020970`; whether
  higher UMDF or WBF layers suppress operations after a failed initial refresh
  is not visible in these DLLs.
