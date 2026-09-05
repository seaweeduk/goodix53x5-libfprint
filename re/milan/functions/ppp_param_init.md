# ppp_param_init

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/address: `ppp_param_init`, `0x180002710..0x180002790`.

## Call Graph

- Exported initialization entry point.
- Logging helper only.

## Behavior

- Validates sensor profile indices `0..12`, then copies the selected seven-field
  table row into preprocessing globals.
- Profile 9's row at `0x1800e46b4` is
  `[1, 0, 0, 800, 88, 108, 12]`: runtime selection threshold `800`, dimensions
  `88x108`, subtype `12`, and packed bit 2 clear.
- The configuration stores do not initialize calibration workspace or the
  auxiliary-count, gain-initialization, and gain-ready globals. The attach path
  `FUN_180030c40 -> FUN_18002b240` forwards engine-context profile field `+0x70`
  to this export. Sample-carried setup refresh instead follows
  `FUN_180031d00 -> FUN_18002bfa0` and does not call this export again.

## Evidence

- Index validation and table stride: complete function body.
- Profile-9 table bytes: `0x1800e46b4..0x1800e46cf`.

## Matcher Geometry Provenance

Independent review confirms that profile 9's `88x108` dimensions are raw
preprocessing dimensions, not the final matcher-area dimensions. The adapter
packs this exact row into `0x36160061`; `FUN_180037c80` decodes subtype `12`,
rows `88`, and raw columns `108`, then the subtype-12 template path normalizes
columns to `104`. Live probe extraction applies the same `108 -> 104` rule in
`FUN_180070690`. The resulting matcher descriptor is width/height `104/88`, and
`FUN_180058700` passes it to the area helper as rows/columns `88/104`.

This chain begins at this export and therefore does not import dimensions from
another profile's table row.

## Confidence

High.

## Unresolved

- Vendor names for the first three packed flag fields.
