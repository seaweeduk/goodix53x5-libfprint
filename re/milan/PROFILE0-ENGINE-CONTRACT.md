# Profile 0 Engine Contract

## Identity And Selection

This note owns the type-0/type-12 preprocessing dispatch comparison in
`GoodixEngineAdapter.dll` 2.0.310.900, image base `0x180000000`. USB profile
selection and algorithm subtype are distinct; see
[the mapping owner](CHIP-ID-PROFILE-MAPPING.md) and
[the USB contract](PROFILE0-USB-CONTRACT.md).

`ppp_param_init` (`0x180002710`) selects 32-byte rows at `0x1800e4590`.
After the profile-index dword, the seven dwords are:

| Field | Profile 0 | Profile 9 |
| --- | --- | --- |
| Floating flag | 1 | 1 |
| Pixel-cancel flag | 0 | 0 |
| Coating flag, already shifted to bit 2 | 4 | 0 |
| Selection threshold | 800 | 800 |
| Rows | 88 | 88 |
| Columns | 108 | 108 |
| Algorithm subtype | 0 | 12 |

`preprocessor_init` (`0x1800027b0`) packs these fields as `0x36160005`
and `0x36160061`, respectively. Both main planes have 9504 samples and
19008 unpacked bytes. The flag values are supplied by the profile table,
not inferred from the image dimensions.

## Preprocessing Dispatch

`FUN_18006d540` receives the packed flags as argument 5 and extracts subtype
with `(flags >> 3) & 0x3f`, rows with `(flags >> 14) & 0x1ff`, columns with
`flags >> 23`, and coating with `(flags >> 2) & 1`.

| Decision in this owner | Type 0 | Type 12 |
| --- | --- | --- |
| Subtype at most 22 and member of mask `0x473912` | False | True |
| Input owner `FUN_180069820` argument 3 | 0 | 1 |
| Foreground-mask construction | `FUN_18006b990` | `FUN_18006a4c0`, then `FUN_18006bce0` |
| Mode derived from mask `0x10110f` | 0 | 1 |
| Member of mask `0xc000000000473ef5` | True | True |
| Auxiliary classifier admitted by mask `0x71810` | False | True |

For the second mask, mode is zero when subtype is at most 20 and its bit is
set, otherwise one. It is passed to `FUN_18006e7f0` and `FUN_18006c510`.
Both types enter `FUN_1800668f0`, `FUN_18006b510`, `FUN_18006e7f0`,
`FUN_18006c510`, and final quality/coverage owner `FUN_180044900`; shared
callers do not imply identical subtype-dependent behavior inside those callees.

With nonnull auxiliary output, both clear its `0x4c98` bytes. Only type 12
then calls `FUN_18006b290` and the following `FUN_180070fc0` mask publication
on this route. Its classifier may override the returned status with `0xc351`;
type 0 cannot obtain that override from this skipped call.

The final coating-only rejection branch is enabled for profile 0 and disabled
for profile 9. It requires quality below 35, subtype other than 20, the
retained local classification result equal to zero, and
`FUN_180069e00(local_workspace, geometry) == 1`. It calls `FUN_180070fc0`
with null mask and mode 1, sets status `0x7531`, and sets the local result to
one. Both types zero quality when final coverage is below six.

## Input And Calibration

`FUN_180069820` clamps every unsigned input sample above 4095 to 4095.
Neither type 0 nor type 12 takes its subtype-16/18 intensity inversion.

Type 0 takes the argument-3-zero border path. For interior columns, saturated
or zero samples in row 1 are replaced from row 2, and those in row `rows-2`
are replaced from row `rows-3`; the resulting rows are copied to the outer
rows. For every row it similarly repairs column 1 from column 2 and column
`columns-2` from `columns-3`, then copies them to the outer columns.
Type 12 instead calls `FUN_1800695e0` first. Failure returns `0x29bb` while
calibration-ready argument 4 is zero, or `0x29aa` otherwise. Success copies
the adjacent rows and columns to the outer border without the type-0 repair
sequence in this owner.

When calibration-ready is zero, `FUN_18006d540` calls `FUN_180064bb0`
with floating flag 1, pixel-cancel flag 0, dimensions 88/108, and the actual
subtype. `FUN_180064bb0` computes the rounded unsigned-sample mean as
`(sample_count/2 + sum(samples)) / sample_count`, storing it through argument
5. It writes the 16-bit baseline plane at workspace `+0x9924` as follows:

| Subtype | Baseline sample |
| --- | --- |
| 0 | Input sample unchanged |
| 12 | Input sample plus `0x1bb7` (7095) |

The type-12 adjustment is selected by subtype at most 22 and mask `0x473000`.
Both return one from this calibration owner on the described route. The
caller then sets calibration-ready to one, zeros quality and coverage, frees
its copied input, and returns zero without live-image processing.

## Calibration Persistence And Reset

Both types use the same `preprocess_get_calidata_len` (`0x180002cf0`),
`preprocess_init_calidata` (`0x1800030d0`), `preprocess_load_calidata`
(`0x180002ed0`), and `preprocess_save_calidata` (`0x180002d00`) exports.
The serialized length is `0x224b0`, distinct from the `0x3048c` workspace
returned by `preprocessor_get_CalibParam` (`0x180002a40`). Their branch
conditions do not test subtype or coating; active plane copy lengths use the
selected geometry, equal for these two profiles.

Default initialization seeds the active accumulation plane at workspace `+4`
to 8192 and baseline at `+0x9924` to zero, zeros workspace count, clears the
`0xa000` bytes at workspace `+0x26488`, and clears dword `+0x30488`. Setup
subsequently replaces the baseline through `FUN_180064bb0`.

The saved layout is: two plane checksums at `+0/+4`, active accumulation
samples at `+8`, baseline at `+0x9928`, `0x800` bytes from global
`0x180218e70` at `+0x13248`, `0x4a40` bytes from `0x180249b00` at `+0x13a48`,
workspace count at `+0x18488`, workspace `+0x26488` data at `+0x1848c`, its
associated dword at `+0x2248c`, and a 32-byte version field at `+0x22490`.
Load requires nonnull input, at least `0x224b0` bytes, matching current version
prefix (up to 32 bytes), and both plane checksums. It returns `0x81` for
invalid input/length or `0x80` for version/checksum mismatch. There is no
explicit subtype field comparison in this export.

Adapter `FUN_18002bfa0` reads a blob with a preceding 16-byte sensor ID and
requires exact sensor-ID equality before loading. A different ID or failed
load initializes defaults. It then calls `preprocessor_init`, retries that
export once on failure, and invokes `FUN_18002aef0` to save after successful
setup. Equal serialization layouts alone do not establish cross-sensor
calibration compatibility; the adapter owns the sensor-ID gate.

`preprocessor_exit` (`0x180002a00`) clears calibrated flag `0x180249afc` and
the `0x3048c`-byte workspace, without clearing the separate process-global gain
planes, auxiliary count, initialization flag, temporal counter, or gain-ready
flag. Those are not part of the serialized workspace count and plane data.
Both profiles therefore use the same reset boundaries, with different
coating/subtype decisions when retained state is next consumed.

### Exported Selection And Global Ownership

The preprocessing exports operate on DLL-global storage, not a device/context
argument. Their bounded writes are:

| Entry | Writes relevant to preprocessing lifetime |
| --- | --- |
| `ppp_param_init` (`0x180002710`) | For profile index 0..12, sets parameter-ready and replaces the seven selected profile fields at `0x180218e50..0x180218e6c`; does not clear calibrated-ready, workspace, or gain state. Invalid index returns `0x81` without replacing them. |
| `preprocess_set_mode` (`0x180003180`) | Writes only calibrated-ready `0x180249afc`; it is not a profile or gain reset. |
| `preprocess_init_calidata` | Initializes the active accumulation/baseline and workspace history/count fields described above; does not clear the separate gain initialization flag. |
| `preprocess_load_calidata` | Replaces the serialized subset after validation; does not restore the separate gain/auxiliary/temporal globals. |
| `preprocessor_init` | Clears calibrated-ready, runs setup, and marks ready after successful baseline construction; does not reset the separate gain globals. |
| `preprocessor_exit` | Clears calibrated-ready and `0x3048c` workspace bytes only; parameter-ready and selected profile fields remain. |

Adapter attach `FUN_18002b240` invokes `ppp_param_init`; adapter detach
`FUN_18002b7e0` invokes `preprocessor_exit` and frees adapter image/enrollment
owners, without an additional preprocessing-gain reset. Setup wrapper
`FUN_18002bfa0` performs sensor-keyed load/default initialization before setup.
None of these bounded chains isolates the separate gains per attached sensor.

The working-frame owner directly supplies these process-global aliases:
`0x1801efbf4` auxiliary count, `0x1801efbf8` initialization flag,
`0x1801efbfc` ready flag, `0x1801fbb70` primary auxiliary gain,
`0x180205490` application gain, `0x18020edb0` secondary auxiliary gain,
`0x1801f9520` temporal reference, and `0x1801efc00` scaled/copied setup scratch.
Temporal-stability counter `0x1801efbf0` has its direct read/write in
`FUN_180064170`. Direct xrefs to the other scalar/map bases originate in
`FUN_1800668f0`; mutation occurs through the passed pointers in its calibration
callees. The setup scratch is replaced on every working-frame call; the gain
and temporal objects have the conditional lifetime described below.

`FUN_1800672e0` tests initialization flag zero at `0x180067317..0x18006732d`
*before* testing workspace count. Only that first arm selects the coating
scalars and initializes the gain planes. A later zero-workspace-count arm
resets the active gain maps, accumulation, and auxiliary count, temporarily
writes initialization zero at `0x180067410`, then writes it back to one at
`0x1800674cf..0x1800674d5`. It does not revisit scalar selection. Thus entering
with initialization nonzero and workspace count zero leaves the preceding
scalar triple selected, even after a new profile selection/default setup.
Changing profiles through these exports is not a complete cold reset.
This is a conditional native state transition, not a claim that WBF schedules
cross-profile reuse within one loaded DLL.

## Live Gain And Retained Calibration

`FUN_1800668f0` selects its scaled branch with subtype mask `0x473932`:
type 0 copies the baseline unchanged and leaves live samples unscaled; type 12
multiplies both by three. Pixel cancellation is disabled for both profiles, so
neither calls `FUN_180063e30`. The final working plane is workspace `+0x13244`.
An application-count sample of zero copies the source directly; otherwise the
rounded numerator uses shift 13 for type 0 and shift 14 for type 12, divided by
the temporary primary divisor. The optional first rerender plane uses the same
numerator but a separate secondary divisor. Zero divisors use the shifted
numerator truncated to 16 bits rather than saturation.

`FUN_1800672e0` uses configuration dword 5 (coating), not subtype, to initialize
the process-global calibration scalars:

| Scalar | Profile 0 | Profile 9 |
| --- | --- | --- |
| `0x1800f28f8` reciprocal-plane numerator factor | 2000 | 8000 |
| `0x1800f28fc` companion calibration scalar | 1600 | 6400 |
| `0x1800f2900` low-count boundary | 30 | 3 |

The three persistent gain planes initialize to 8192. Workspace count zero
additionally clears the per-pixel accumulation plane, auxiliary counter, and
initialization state. A nonzero count at or below the low-count boundary resets
the application gain to 8192. Above that boundary, initialization-state zero
permits normalization of the accumulated plane by its rounded mean; a zero
mean resets workspace count to zero. These are shared state transitions with
different coating-selected boundaries, not independent per-frame constants.

`FUN_180067500` uses mask `0x473912` for its source/admission split. For type 0,
the source is `max(setup-live, 0)` and update admission comes from
`FUN_180066810`; for type 12 it is `max(9000+3*(setup-live), 0)` and admission
requires `mask_count*10 > sample_count*9`. Dword 8 (selected-plane entry state)
equal to 2 disables update admission for either. Both share temporary-gain
construction through `FUN_180066690`, temporal gating through `FUN_180064170`, the two
`FUN_180063a80` auxiliary updates, and the final `FUN_180067050` commit when
the ratio plane exists. Each receives the actual subtype where provided.

For type 0, let `P` be temporary gain, `A` application gain, `B` primary
auxiliary gain, `C` secondary auxiliary gain, `N` workspace count, and `K`
auxiliary count, observed before this call's auxiliary/commit updates. With
`q(x,y)=(x*y+4096)>>13`, when `K<5 && N>30`, primary
divisor is `q(P,A)` and secondary divisor is `P`; otherwise primary is
`q(q(P,A),B)` and secondary is `q(P,C)`. Type 12 instead uses the ready flag
and boundary 3 to select these products, as detailed in
[the working-frame owner](functions/FUN_1800668f0.md). Both evaluate the shared
ready transition: set when `K==0 && N>5`, clear when `K>14`; type 0's divisor
branch does not consume that flag.

The type-0 `FUN_180066810` admission counts source samples whose signed
absolute value is strictly above 100, and accepts only when that count exceeds
`(9504*205)>>8`, namely 7610 (at least 7611 samples). This is separate from
foreground-mask coverage. `FUN_180066690` changes a temporary gain only when
the smoothed/original source difference exceeds 600 for type 0 or 1800 for
type 12 and both source terms are nonzero. Its ratio result truncates to 16
bits for type 0; type 12 caps it at 32767.

`FUN_180064170` samples every second row/column into a retained 54x44 signed
short plane. With nonzero auxiliary count it computes integer mean absolute
difference from that plane. A mean below 15 (type 0) or 30 (type 12) increments
global `0x1801efbf0` without replacing the reference. Otherwise, or when the
auxiliary count is zero, it replaces the reference and resets that counter.
Admission is counter at most 10 for type 0 versus at most 3 for type 12.

`FUN_180067050` admits each ratio sample to the running accumulation when
`abs(ratio-2000)<1600` for type 0 or `abs(ratio-8000)<6400` for type 12.
Neither type selects its mask-`0x412000` extra early-count admission. It
increments the workspace count, caps it at 400, and normalizes application
gain when the incremented count exceeds 30 or 3 respectively and the rounded
accumulation mean is nonzero. Type 0 additionally filters the normalized map
for counts 31..200 using `FUN_18007be70`, kernel selector
`FUN_18007e4a0(5)`, and both scalar arguments `0x320000/count`. Type 12 skips
this map-filtering arm (mask `0x473112`).

## Type-0 Foreground Mask

`FUN_18006b990` calls `FUN_18006e0e0` for threshold `T`. With floating flag 1,
type 0 collects positive `setup-live` differences strictly above 50. If none
exist, `T=50`; otherwise `T=floor(floor(sum/count)/5)`, raised to at least 50
when the contributing count is at most `columns*10` (1080). More than 1080
contributors bypass that floor. The mask writes 255 unless `setup-live<T` or
the live sample is 4095, in which case it writes zero. The signed-short header
at `+0x0c` stores `T`; `+0x0e` stores integer valid percentage. Its invalid
counter can count a threshold-rejected saturated sample twice; percentage is
derived from that counter, not recomputed from mask bytes.

Type 0 uses this one mask for both calibration and rendering. Type 12 has the
separate `FUN_18006a4c0` mask and `FUN_18006bce0` rendering clone. Consequently
equal dimensions do not imply equal mask ownership or admission counts.

## Candidate Selection And Post-Render Policy

The primary `FUN_18006b510` renderer shares range construction and inverse
8-bit quantization, but its `FUN_18006cbc0` preparation is different. Type 0
copies the working source without either the mask-`0x473912` correction
(`FUN_180065dd0` then `FUN_180065bf0`) or the coating-clear
`FUN_18007be70` filter. Type 12 runs both corrections and that filter with
selector 10. The renderer's low-range diagnostic threshold is 50 for type 0
and 150 for type 12 (mask `0x473800`). Both mark diagnostic availability only
when low-range count is greater than `sample_count/5`.

Both refined rendering (`FUN_18006d430`) and quality rerendering
(`FUN_18006d190`) select masked preparation when the signed mask percentage is
below 96, otherwise unmasked preparation. Mode 0/type 0 calls
`FUN_180069730` or `FUN_18006b170`; mode 1/type 12 calls `FUN_180069ce0` or
`FUN_180069bc0`. The refined owner then uses `FUN_18006b510`. The quality
rerender owner instead uses `FUN_18006a1f0`, whose only subtype branch is for
type 11, so its range/quantization kernel is shared unchanged for types 0/12.

Mode 0 centers each row; mode 1 centers each column. Both use integer unsigned
sample means and produce signed-16 `sample-mean+5000`, clamped to zero when
the signed result is nonpositive. Masked variants use only nonzero-mask samples
in the mean and write 5000 outside the mask; a line with no included samples
uses mean zero. Unmasked variants include every sample. These are distinct
axis implementations, not different thresholds on the same axis. Consistently,
`FUN_180069150` scores adjacent row-mean differences whereas `FUN_180069350`
scores adjacent column-mean differences.

`FUN_18006e7f0` shares the ordered replacement formula but not all inputs:
type 0 uses `FUN_180069150` and the foreground mask at mask `+0x10` because
coating is set. Type 12 uses `FUN_180069350` and the independent primary-image
mask from `FUN_18007e4b0`. Both use initial selection threshold 800. The three
type-0 transition boundaries are 2000/4000/6000 versus type-12
1800/3600/5400. Correlation threshold 235, strict comparison ties, primary-first
copy, and selected-plane publication are shared. See
[candidate selection](functions/FUN_18006e7f0.md).

`FUN_18006c510` computes type-0 disagreement from the optional secondary-divisor
working plane, versus workspace `+0x13244` for type 12. Both rerender that
optional plane through `FUN_18006d190` when candidate 1 was selected, or
`FUN_18006a1f0` when candidate 0 was selected. Type 0 admits the quality arm
when the selected render-difference metric is strictly above 50; coating,
workspace count below 100, and purpose 1 together lower that threshold to 35.
It has no reciprocal-plane fallback admission. Type 12 uses threshold 75 and
its 56..75/secondary-difference-below-80 fallback. The field-7/purpose
alternative does not admit either profile, whose field 7 is zero.

On an admitted arm both temporarily force workspace count to 5 for a nested
`FUN_1800668f0` update when saved count is below 100 and raw disagreement is
above 90, then restore only the count. For type 0 that forced count remains
inside the coating-selected low-count reset range 1..30; for type 12 it is
outside 1..3. Both copy the selected byte image to both retained byte planes
without rerendering it after the nested update.

Type 0 returns `0x7531` on raw disagreement above 40 and publishes metadata
mode 2 because coating is set; otherwise an admitted arm publishes mode 4.
It has neither type-12 normalized-disagreement rejection above 33 nor type-12
mode-3 refinement (count below 100 and metric above 85). An unadmitted arm
publishes mode 0. These metadata writes precede the caller's optional
classifier, final quality/coverage, coating check, and selected-plane copy.

The disagreement helper `FUN_18006c0b0` retains the same residual admission
thresholds for both profiles: more than 30% of included mask samples absent
from its derived mask, followed by residual quality below 15. Its histogram
renderer `FUN_18006de90` inherits the different `FUN_18006cbc0` preparation
and uses minimum quantization range 100 for type 0 versus 200 for type 12.
Residual quality goes through `FUN_180043cb0` into `FUN_180044530`, with
type-0 control values 20/20 versus type-12 25/30. Thus the residual decision
has shared orchestration but still subtype-dependent quality inputs.

`FUN_180044530` does not consume its packed-flags argument: this residual
quality difference is parameter-only within that owner. Both call
`FUN_180043d90` with flags 0/1, evaluate `FUN_180044b50` when the former score
is below 70, apply quadratic attenuation below the supplied 20/25 threshold,
and add 15 when the latter scalar reaches the supplied 20/30 threshold.

`FUN_180070fc0` is shared unchanged and has no subtype argument. Mode zero
returns before writing image metadata or applying a residual mask. Type 0
can publish post-render modes 0/2/4 and final coating mode 1; type 12 can also
publish mode 3 and has the separate auxiliary-classifier metadata call.
The nonnull auxiliary buffer remains entirely zero after a type-0 call on this
route: its `0x4c98`-byte clear is not followed by `FUN_18006b290`. Exported
`preprocessor` does not subsequently fill it or synthesize type-12 classifier
bytes. For both profiles, exported `preprocessor` rejects liveness-switch
argument 6 equal to 1 with `0x81`.

### Image Metadata At The Extraction Boundary

The zero type-0 auxiliary buffer is separate from metadata embedded in image
LSBs. `FUN_180070fc0` operates on the selected 108-column image before the
export copies it. The marker is alternating even-zero/odd-one LSBs in first-row
columns 0..99; columns 100/101 carry the low mode, column 102 marks the mask,
and columns 103..105 carry the high mode. A nonzero encoder mode repairs an
invalid prefix and initially clears the last eight LSBs. A valid existing
prefix preserves unrelated fields. Mode zero does not inspect or clear them.

| Type-0 producer arm | Encoder mode | LSBs at columns 100/101 | Packed low value from `FUN_18006fb10` | Low class from `FUN_180070d90` |
| --- | --- | --- | --- | --- |
| No post-render admission, no coating rejection | 0 | Unchanged image bytes | No forced value | No forced value |
| Post-render admitted, raw disagreement at most 40 | 4 | 0/0 | 0 | 0 |
| Post-render admitted, raw disagreement above 40 | 2 | 1/1 | 2 | 2 |
| Final coating rejection | 1 | 1/0 | 1 | 2 |

The final coating arm requires the preceding local mode to be zero, so it
does not replace a type-0 mode-2 or mode-4 result. Type 0 has no deliberate
mode-3 or high-field encoder call on this route. Type 12 additionally emits
mode 3 (bits 0/1, packed low 3, decoded low class 3) under its post-render
refinement, and its classifier may encode high-field modes 5..9. Mode zero
does not prove absent metadata, and type 0's lack of high-field writes does
not prove high class zero when the rendered image already has a valid prefix.

The post-render residual-mask flag remains independent of mode. When it is
set and mode is nonzero, the encoder sets column 102 and writes inverse mask
LSBs after the first row. With an existing mask marker it only clears LSBs
where the supplied mask is nonzero; it does not reset all previous bits.
Mode zero ignores even a set apply-mask flag. Final coating mode 1 passes a
null mask and apply flag zero. Type 0 therefore has an image-mask publication
path despite skipping auxiliary classification.

`FUN_18006fb10` validates this prefix before publishing the packed mode. It
maps raw bit pairs 0/1/2/3 to packed low values 0/1/3/2; the packed high bits
are placed at bits 8..10. Its return reports allocated image-mask availability,
not whether mode decoding occurred. `FUN_180070d90` maps packed low values
0/1/2/3 to classes 0/2/2/3. These are the preprocessing products consumed by
the extraction owner; subsequent feature-class promotion, `c7` publication,
matcher admission, and retry handling belong to
[the extraction boundary](functions/FUN_18006fb10.md) and
[the compact-class owner](functions/FUN_180070d90.md). A status-bearing image
product alone does not establish that the adapter submits it to matching.
The purpose-specific status admission and subsequent metadata consumers are
owned by [the adapter comparison](PROFILE0-MATCH-TEMPLATE-CONTRACT.md#adapter-image-admission).

## Final Quality And Coating Rejection

The shared entry `FUN_180044900` does not mean shared quality computation:
mask `0x41f912` sends type 0 to `FUN_1800438f0` and type 12 to
`FUN_180044300`. On the one-byte image format supplied by preprocessing,
type 0 obtains Q16 coverage through `FUN_18007e4b0` with threshold 120, then
calls `FUN_1800446f0`. Coating selects `FUN_180043d90` arguments 0/0 and a
separate filtered quality image: `FUN_180049af0`, `FUN_18007be70` selector 6,
`FUN_180049b60`, then `FUN_180044b50`.

Let `Q` be the `FUN_180043d90` result and `M` the last helper's scalar.
When `M<45 && Q<90`, type 0 applies `F=M*256/45`, then
`Q=((Q*F)>>8)*F>>8` in that order. When `FUN_1800446f0`'s scalar minus Q16
coverage is strictly above `0x3333`, it additionally scales quality by coverage
twice with division after each product. Type 0 subtracts one, clamps quality
to 0..100, converts coverage by `coverage*100>>16`, and returns zero. In
contrast, type 12's `FUN_180044300` can return `0x7532`, adds seven to positive
quality, and does not take its coverage-squared subtype adjustment. See
[quality dispatch](functions/FUN_180044900.md).

For the caller's final profile-0-only coating predicate, `FUN_180069e00`
first requires the primary-render diagnostic availability flag. Its
`FUN_18006e660` call summarizes the 200-bin range histogram: `L` at `+0x4fbc`
is the first ascending bin whose cumulative count times 100 is strictly above
30 times the total; `H` at `+0x4fb8` is the first descending bin whose
cumulative count exceeds integer `total/10`. The predicate returns one when
`L<20`, or `3*L<H && L<40`, or `diagnostic[+0x4fb4]>sample_count/4 && L<60`.
The `+0x4fb4` dword is histogram bin 199:
`0x4c98 + 199*4 == 0x4fb4`. It counts included-mask samples whose local
upper-minus-lower range is at least 199. The renderer clears all 200 bins with
`memset(diagnostic+0x4c98,0,0x320)` at `0x18006b75c..0x18006b78b`, covering
`+0x4fb4..+0x4fb7`. When low-range count exceeds `sample_count/5`, it enables
the diagnostic and accumulates `histogram[clamp(range,0,199)]` at
`0x18006b900..0x18006b940`. The indexed increment is at `0x18006b92e`.
Thus the final alternative tests whether more than one quarter of all image
samples occupy the saturated high-range bin while `L<60`.

The diagnostic allocation is malloc-backed and is not blanket-zeroed by
`FUN_18006d540`, but every field read by this coating predicate has a producer
in the bounded renderer/histogram chain. `FUN_18006e660` supplies only the two
quantile fields `+0x4fb8/+0x4fbc`; the renderer supplies the histogram and
sample count at `+0x4fc0`. The caller additionally requires quality below 35
and preceding local classification zero, as described above.

In `FUN_18006d540`, allocation size `0x4fc4` is passed at `0x18006d7d4` and
the result stored in stack slot `[rbp-0x40]` at `0x18006d7e9`. That same
pointer is supplied only as the primary renderer's fifth argument at
`0x18006daeb..0x18006db03`, the coating predicate's first argument at
`0x18006dd93..0x18006dd9b`, and the final free at
`0x18006de1d..0x18006de21`. Candidate rendering receives a null diagnostic
pointer. Type-0 foreground masks, optional working planes, and the auxiliary
output are distinct owners; the diagnostic pointer is not passed through
them. `FUN_180003800` at `0x180003800..0x180003802` zero-extends the requested
size and jumps to the malloc import. No allocator-clearing assumption is
needed for the coating inputs because their explicit writes cover them.

## Template Construction

`FUN_180037c80` explicitly groups subtype 0 and subtype 12 in the same switch
arm. Both change raw columns 108 to 104, preserve 88 rows, set header flags
at `+0x0c/+0x10` to `1/1`, set both record-capacity words at `+0x14/+0x18`
to 150, and set feature capacity at `+0x20` to `min(requested, 40)`.
The subtype stored at header `+0x00` remains distinct: zero versus twelve.
Both allocate the same geometry-dependent gallery and twenty cache owners.
See [the constructor owner](functions/FUN_180037c80.md).

This common constructor contract does not establish equivalent extraction,
matching, study, or persisted-template compatibility between the subtypes.
