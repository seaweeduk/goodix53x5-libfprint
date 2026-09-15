# enrolStartEx

## Binary And Body

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `enrolStartEx`, `0x1800010d0..0x18000133a`.
- Role: allocate and initialize the live enrollment template after
  `ppp_param_init`.

## Call Graph

- Adapter callers include `FUN_18002d280` and `FUN_1800303c0`.
- Relevant callees are `FUN_180037c80` (`newTemp`) and `FUN_180041800`.

## Profile-9 Provenance

The export refuses to proceed until the preprocessing profile globals are
initialized. It packs those globals into the chip-information word passed to
`FUN_180037c80` without selecting an unrelated matcher profile.

For the profile-9 table row `[1,0,0,800,88,108,12]`, the packed word is
`0x36160061`. `FUN_180037c80` decodes subtype `12`, rows `88`, and raw columns
`108`, then applies subtype-12 match normalization to columns `104`. The live
template therefore begins `[12,104,88,1,1,150,150]`.

## Mode-0 Session Ownership

The export returns the allocated 32-byte session pointer in `RAX`, rather than
a status code. Its first qword owns an eight-byte wrapper whose first qword is
the public template handle; that handle in turn points to the live template.
Session `+0x08` stores the requested signed16 count, `+0x0a` starts at zero,
and the progress/overlap dwords `+0x0c..+0x14` start at zero.

The first argument is an in/out physical-capacity pointer. The export initially
caps it at 50, passes that capacity to `FUN_180037c80`, then replaces it with
the resulting template maximum. In mode zero, the third argument is the
independent requested count; a negative value or a value above the physical
maximum rejects construction. Requested count zero is not rejected here.
Successful construction returns the session at `0x1800012af`; allocation and
configuration failure paths return null. `enrolFinish` owns teardown.

## Instruction Anchors

- Profile-global packing and `FUN_180037c80` call: `0x180001135..0x1800011e6`.
