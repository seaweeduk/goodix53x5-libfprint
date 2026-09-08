# Type-0 And Type-12 Match/Template Contract

## Scope And Identity

This note compares algorithm subtypes 0 and 12 in `GoodixEngineAdapter.dll`
2.0.310.900. Profile 0 selects type 0; profile 9 selects type 12. Preprocessing
belongs to `PROFILE0-ENGINE-CONTRACT.md`; USB behavior belongs to
`PROFILE0-USB-CONTRACT.md`. Equal template geometry does not erase subtype.

## Extraction Dispatch

`FUN_18004ae70` is the common enrollment/identify feature owner. Configuration
word zero carries subtype; words 5 and 6 supply enrollment and identify record
limits respectively. Both types use centered geometry through `FUN_180070690`,
Gaussian filter 6, `FUN_18006fef0` bitmap production, and the ordinary
`FUN_180049b90` record-materialization family. Both use the same final per-record
`FUN_180047ae0` transform, selected by the caller's enrollment/identify mode,
followed by `FUN_180037480` partitioning.

The following branches differ inside this common owner:

| Boundary | Type 0 | Type 12 |
| --- | --- | --- |
| `0x413000` record-limit mask | Configured limit unchanged | `max(1, trunc(coverage * limit / 100))` |
| `0x6138c0` materializer configuration word 4 | 0 | 1 |
| Auxiliary classifier `FUN_180048260` | Skipped | Called when auxiliary pointer is nonnull |
| Classifier-owned feature `+0x158` allocation | Skipped | Allocated when needed, before classification |
| `0x4000000000260ec1` post-materialization filter | Calls `FUN_180048b00` | Skips helper |

The shared configured record limits are 150 in the ordinary constructor.
`FUN_180049b90` uses configuration word 4 to permit a second scale-space pass
when the first pass finds at most 60 records. Type 0 therefore does not take
that retry; type 12 can. Both allocate six scale-space owners, use the ordinary
rank/truncate path, and quantize record coordinates and angle afterward.

`FUN_180048b00` reads the full-resolution validity matrix. It rounds unsigned
Q8 record coordinates as `(coordinate + 0x80) >> 8`, removes records whose
in-bounds mask byte is zero, swaps in the final active 56-byte record, clears
the vacated slot when a swap occurs, and revisits the swapped-in record. It
changes feature count `+0xf0`, not any feature matrix. This type-0 removal is
separate from the later decoded broken-mask filter `FUN_18004db10`.

Both types decode incoming descriptor metadata and eventually store its final
value at feature `+0x150`. Only type 12 can overwrite that value through the
auxiliary classifier in this owner. Neither the common geometry nor a shared
record layout establishes identical feature output.

The zero type-0 auxiliary buffer does not imply zero image metadata. The
[engine metadata producer](PROFILE0-ENGINE-CONTRACT.md#image-metadata-at-the-extraction-boundary)
publishes post-render mode 4 with packed low value zero, post-render mode 2
with packed low value two, and final coating mode 1 with packed low value one.
The latter two decode to low class two. Encoder mode zero leaves prior image
LSBs untouched. Type 0 has no deliberate high-field encoder, but an already
valid image prefix can retain high fields. Neither newly produced type-0
features nor their packed `c7` can therefore be declared uniformly zero.

`FUN_18004ae70` initializes its packed metadata local to zero, decodes the
original processed image through `FUN_18006fb10` before centered cropping,
and stores that value at feature `+0x150` after materialization. Type 0 skips
the auxiliary-classifier overwrite, not this image decoder. Decoder return one
means an allocated image mask: it separately enables `FUN_18004db10`, then the
mask is freed. A return other than one does not mean metadata was absent.
Zero `c7` is valid only when the complete decoded packed value is zero, such
as an invalid prefix or zero low and high fields, not merely zero auxiliary
data or a mode-4 low field. Imported templates additionally restore supplied
`c7` directly.

## Adapter Image Admission

`FUN_18001f610 -> FUN_180032040 -> FUN_18002c810` is the sample preprocessing
chain. `FUN_180032040` selects enrollment purpose one only when retained sample
purpose `+0x124` equals four; otherwise preprocessing purpose is zero.
`FUN_18002c810` returns transport success separately from its status output.
Its policy has no type-0/type-12 selector:

| Raw preprocessing status | Identification | Enrollment |
| --- | --- | --- |
| `0x29aa`, `0x7531`, `0xc351` | Clear status to zero, then require nonzero quality and coverage | Return raw status before the quality/coverage check |
| Zero | Require nonzero quality and coverage | Require nonzero quality and coverage |
| Other nonzero status | Not cleared; zero quality or coverage can replace it with `0x80` | Not cleared; zero quality or coverage can replace it with `0x80` |

The identify remapping is at `0x18002cfb2..0x18002cfea`; the enrollment early
return is at `0x18002ce7f..0x18002ceaf`; the shared zero-metric check is at
`0x18002cff0..0x18002d010`. Thus `0x7532`, unlike `0x7531`, is not an admitted
status. The admission test is nonzero, not the later enrollment or failed-match
quality/coverage thresholds. Nonzero metrics also permit the calibration
snapshot; they do not clear an unlisted status.

`FUN_18001f610` tests the separate preprocessing status and maps nonzero to
bad-capture HRESULT `0x80098008` with reject detail 10. Only status zero permits
`FUN_1800324f0`, either for enrollment purpose four or the separately enabled
immediate-processing route. A type-0 `0x7531` enrollment image therefore does not reach feature
insertion on this sample path. This rejection does not roll back preprocessing
state already changed by the frame.

For identification, clearing `0x7531` does not clear or re-encode processed
image bytes. The later storage comparison owner `FUN_18002e360` passes retained
processed descriptor `0x180196c80` and the auxiliary pointer to `identifyImage`
after storage-envelope admission. The export copies the processed pixels and
calls `FUN_18004ae70` in identify mode, then builds anti-fake state and calls
`FUN_18005edb0 -> FUN_180055a40`. It does not receive the original preprocessing
status or reject positive low image metadata before extraction. Consequently,
type-0 mode-2 and coating-mode-1 images with nonzero quality/coverage can reach
matching with low class two. This establishes admission, not successful match:
record extraction, gallery policy, and candidate scores remain subsequent gates.
The type-0 terminal `-32` predicate below is not excluded by metadata provenance;
its remaining coverage, feature-mode, high-class, auxiliary-class, and score-zero
conditions must still hold.

## Matcher Configuration And Dispatch

`FUN_18005edb0` builds policy with `FUN_18005e230` from the enrolled template,
then sets word 18 from live probe `+0x150` and word 19 to one. Both types pass
family mask `0xc0000000006138cd` and call `FUN_180055a40`. Dispatch is based on
the gallery subtype, not a subtype field in the live feature.

For both types, policy offsets `+0x18..+0x2c` contain
`[23,47,40,38,-1,16]`; word 13 is one, word 16 is zero, and word 15 retains
the actual subtype. Area factor at `+0x30` is signed integer
`(columns * rows * 256) / 0x2520`. Word 17 (`+0x44`) differs: bitset
`0x473000` supplies zero for type 0 and one for type 12. The common dispatcher
does not remove this distinction before calling the matcher.

## Candidate Policy Differences

`FUN_18005a3c0` and its `FUN_180078a70` callee select pair capacity 31 for
type 0 versus 42 for type 12 with mask `0x413000`. Both use two partitioned
`FUN_180076400` scans and `FUN_180075a60`, but pass the different capacity to
pair selection and `FUN_18005b900`. In `FUN_180055a40` the matching score uses
the same type-dependent denominator: each retained candidate contributes
`(pair_count * 256 + (capacity >> 1)) / capacity`; the published aggregate is
`(sum * 100 / retained_count) >> 8`.

`FUN_18005a3c0` mask `0x613800` enables early `FUN_180071d40` metrics for
type 12 and skips them for type 0. In the alternate-transform pass, type 12
selects by adjusted image metrics; type 0 adopts the alternate affine when its
pair count is larger, before the common topology checks.

The sibling owner `FUN_180057900` carries the same 31/42 capacity split and
`0x613800` metric-selection distinction. Type 12 can replace the affine through
the adjusted-image-metric branch; type 0 installs the sibling affine at the
later successful topology gate instead. Both clamp the resulting primary and
alternate pair counts to their respective capacities when topology exceeds 30.

Both types call preliminary policy `FUN_180054ec0`. Its first eight-entry
threshold table differs (the remaining two eight-entry tables are shared):

| Type | Threshold entries 0 through 7 |
| --- | --- |
| 0 | `0x0fffffff,0x0fffffff,209,208,208,207,207,207` |
| 12 | `0x0fffffff,0x0fffffff,220,216,215,212,211,210` |

The table index is `clamp(adjusted_candidate_word1 - 7, 0, 7)`;
adjustment accounts for topology and, when enabled, low overlap. The tested
score is candidate word 5, with a four-point reduction when candidate word 0
is below five. This is only one conjunct of the helper's match policy, not a
standalone acceptance threshold.

The later dispatcher `FUN_180053220` selects these distinct descendants:

| Stage | Type 0 | Type 12 |
| --- | --- | --- |
| Initial missing-flag classifier | `FUN_180054ec0` | `FUN_18005c0c0` |
| Nonzero match-flag veto | `FUN_18005f150` | `FUN_180058ee0` |
| Nonzero candidate-flag continuation | `FUN_1800602e0` | `FUN_180053680`, only with match flag zero |
| Recognition-one final classifier | Skipped | `FUN_1800572e0`, when match flag is zero |

The common intermediate `FUN_180054930` call is conditional on candidate flag
zero. Type-0 `FUN_1800602e0` has an additional exact-type-zero tail: with
candidate flag nonzero, match flag zero, policy word 16 zero, candidate word
11 `<35`, word 0 `<10`, word 1 `<18`, `word8 + word5 - config.word0 <415`,
and `word5 - config.word0 <211`, it clears the candidate flag.

Policy word 17 has a real consumer in type-0 `FUN_18005f150`: when it is not
one, subtype is not 11, match flag is nonzero, candidate word 4 is `128`, and
candidate words 5+8 sum to less than `360`, the helper clears match flag.
Types 0/12 both have word 16 zero, but only type 0 enters this veto family.

Both subtypes select the same alternate proximity table in `FUN_1800624c0`:
modes 1/2/3 use inclusive absolute identity deltas
`[25,21,1792,21,25,1792]`, mode 4 uses `[38,38,3072,38,38,3072]`, and mode 5
uses `[45,40,3840,40,45,3840]`. Modes outside 1..5 return zero. The caller
decides whether a positive proximity result is a veto.

`FUN_180055a40` additionally has a type-12-only positive-metadata geometry
ladder and a type-12-only anti-fake rejection ladder after `FUN_18003a3e0`.
Type 0 bypasses both exact `subtype == 12` branches. Conversely its terminal
score-zero path can publish `-32` under mask `0x40000000000008cd`, positive
decoded low class, coverage `<65`, ordinary feature mode, high class `<4`, and
auxiliary class `<2`; type 12 is absent from that mask. Both may reach
`FUN_18005d5f0` through the other terminal path. The preceding producer and
policy differences remain inputs to all common downstream helpers.

The common fallback `FUN_18005d5f0` uses 31 pairs for both subtypes when
calling `FUN_18005b900`, but its terminal `FUN_1800608b0` policy differs.
Let `N=candidate.word1`, `S=candidate.word5`, `D=candidate.word8`, and
`O=candidate.word9`. For type 0 (configuration word 17 zero), one of these
six conjunctions must hold:

| Minimum N | Minimum S | Minimum D | Minimum S+D | Minimum O |
| --- | --- | --- | --- | --- |
| 5 | 226 | 176 | none | 91 |
| 5 | 218 | 176 | none | 121 |
| 7 | 216 | 176 | none | 121 |
| 8 | 209 | 191 | none | 106 |
| 9 | none | none | 416 | 41 |
| 8 | none | none | 426 | 41 |

For type 12 (word 17 nonzero), if candidate word 0 is below word 1 the helper
first subtracts two from both S and D. It then requires `N>=8`,
`S+D-4*(word12+word13+word14)>=417`, and `O>=96`. For either type, admitted
score is `(((N*256 + (capacity>>1))/capacity)*100)>>8`, with capacity 31/42
for type 0/12 respectively; rejection returns zero. `FUN_18005d5f0` caps a
positive score at 100. This shared fallback body therefore does not imply
shared fallback acceptance or scoring.

## Secondary Geometry And Rescue

`FUN_18005b900` receives the gallery, probe, selected feature index, explicit
pair scan capacity, strict minimum count, estimator argument, pair slots, and
affine output. It selects a physical gallery feature for indices below 50 and
the queue-backed feature otherwise. It compacts valid first indices, reads Q8
coordinates and signed angles from records, and returns zero when the compacted
count is at most the supplied minimum. `FUN_180077e20` fits the affine;
`FUN_180057140` rejects orientation-inconsistent pairs; `FUN_180076af0` refines
when at least four survivors remain and residual exceeds `0x4000`. This owner
neither reads subtype/coating nor forwards them to its fitting kernels. The kernel
is shared, but its scanned pair set is caller-owned: initial matching supplies
31/42, while fallback supplies 31 for both types.

The three secondary selectors receive feature objects, not an outer template
as their first argument. They read counts at `+0xf0`, records at `+0xf8`,
stored partition boundaries at `+0x118`, dimensions from feature words 0/1,
and an explicit subtype argument six. All select capacity 31 for type 0 and
42 for type 12 through mask `0x413000`, then pass it to `FUN_180075a60`.
Their geometry descriptors copy threshold words 0, 1, and 5 from argument five.

- `FUN_180076850` calls `FUN_180074ba0` separately for the leading and trailing
  stored partitions, passing the incoming affine and argument-seven context.
  Its special `FUN_1800756b0` route selects neither type 0 nor type 12.
- `FUN_1800749c0` uses the same partition ranges with `FUN_180075dc0` and the
  incoming affine, then performs one capacity-bounded pair selection.
- `FUN_180075220` builds byte-index lists from record dword `+0x0c` exactly
  equal to one or two. Within each stored partition it calls `FUN_1800770e0`
  for opposite-class pairs in both directions, accumulating two independent
  candidate tables. Two `FUN_180075a60` calls publish the two pair outputs.
  Counts, classes, and partition membership are real input constraints; the
  common body does not make their extracted inputs equal.

None of these selectors reads coating. Beyond capacity, their subtype
predicates choose the same arms for 0 and 12.

The generic scan descendants do not receive subtype or coating either.
`FUN_180074ba0` borrows the byte distance matrix, with physical row stride 180
and `0xff` denoting unavailable pairs; it does not recompute descriptors.
`FUN_180075dc0` instead computes the minimum Hamming distance between record
`+0x28` and the other record's `+0x28`/`+0x30`, bounded by descriptor word eight
(the selector's threshold argument word five). Both use affine-guided spatial
and border bounds. `FUN_1800770e0` receives distance-availability and layout
matrices with the same 180-byte stride; layout zero chooses the other record's
`+0x30`, nonzero chooses `+0x28`, and the same threshold bounds the 64-bit
Hamming distance. These matrices and class lists are borrowed caller inputs,
not subtype-independent constants.

`FUN_180054930`, reached from `FUN_180053220` only with candidate flag zero,
uses the same three ten-entry threshold tables and penalties for both types:
configuration word 16 is zero for both, and the only exact-subtype exception
is type 63. It reads candidate metrics and configuration, not its two image
scalar arguments. The complete broad/strict equations are in
`functions/FUN_180054930.md`. Shared equations do not imply equal flags because
earlier candidate metrics and dispatcher reachability differ.

`FUN_180072860` also selects identical publication behavior for types 0 and 12.
It lazily constructs two source-feature record maps split at `+0x118`, builds
an alternative affine with `FUN_180073ba0`, and evaluates `FUN_180071d40` with
null optional mismatch/reclassification outputs. It publishes only if the
alternative overlap is not 128 and either incumbent overlap is 128 or the
alternative detail is strictly greater. Coverage becomes
`(primary_coverage * context_scale) >> 8`. Both call `FUN_180074e80` with the
actual subtype and unchanged candidate word one before publishing the affine.
Neither enters the special packed-coverage or special geometry branches.
`FUN_180074e80` confirms that downstream classification: both use same-low-two-
flag nearest neighbors with squared Q8 distance strictly below `0x40000` and
the ordinary minimum descriptor distance, with no descriptor-distance veto on
the neighbor count. It clears eleven result words and publishes record-domain
percentages using unchanged retained count, not a bitmap population.
The same rule can replace geometry without recomputing candidate counts or
affine penalties; see `functions/FUN_180072860.md`.

`FUN_18005d9e0` uses the same uncovered-area rescue gate for both types.
Its caller at `FUN_180055a40:0x180056c75..0x180056caf` requires contributor
count greater than one, evidence `+0x688` zero, and disqualifying count zero.
The explicit exclusions are types 11 and 21, not type 0 or type 12.
It iterates lifecycle order `+0x87e8`, not physical slots, and accepts candidate
rows only with word one `>4` and word five `>189`. Each affine removes coverage
from the same temporary probe mask, so weighted contributions are sequential
marginal areas. Both use large-area threshold 1400 and require more than one
eligible row, nonzero total area, weighted word-five average `>208`, unweighted
word-one average `>6`, weighted word-eight average `>183`, and more than one
area increment `>1400`. Both use the same stronger affine-publication rule:
incoming score `>40` or more than two large increments. The types-11/21
exception applies to neither. Ordinary acceptance writes evidence `+0x684`,
selected index `+0x648`, and `best_count*100/denominator`, with denominator
31 for type 0 and 42 for type 12. Only strong publication also sets `+0x688`
and copies the selected affine; otherwise prior geometry remains unchanged.

## Study Dispatch

`templateStudy` reaches `FUN_180044fc0` with the retained matched gallery,
live probe, and original match evidence. Both types take the ordinary family,
not the types-9/10/17/18 branch. Both use quality `>15` and coverage `>65` for
primary selection through `FUN_1800469f0`; the coverage exception for type 11
does not apply. Both share retained-feature refresh, exact-capacity queue
shutdown, twenty-owner queue storage, continuation through `FUN_18005d330`,
and the late-enqueue gates documented in `functions/FUN_180044fc0.md`.

The final transaction step differs. When original evidence `+0x684 != 0`, mask
`0x413002` selects `FUN_1800607b0` for type 12 but `FUN_180054830` for type 0.
Both then increment gallery dword `+0x8cf4` once. This selection occurs after
primary policy, queue continuation, shutdown, and late enqueue; it does not
test the published action.

Both finalizers selection-sort the live index vector at `+0x87e8`, leaving
feature objects in their physical slots. Primary priority is descending signed
feature `+0x110` for both. Type 0 breaks that tie by ascending signed `+0x134`,
then ascending signed `+0x12c`; type 12 instead uses descending values for both
tie breakers. Equal keys do not replace the current selection. The outer
counter starts at one and selects slot `counter-1` from the remaining suffix,
including the final two-slot comparison. Swapping means this is not a stable
sort even though an equal comparison does not replace the current selection.

`FUN_1800469f0` uses the same ordinary-family primary selector for both types:
append below capacity; at capacity require replacement enabled, probe quality
`>15`, and either selected-gallery quality `<60` or
`gallery_quality*6 < probe_quality*10`. `FUN_180045530` receives no subtype
parameter and reads no template subtype; its replacement ranking is shared.
The primary selector invokes the same normalization owner after a capacity-
reaching append or an action above two.

`FUN_1800462a0` uses the same twenty-slot rank policy for both types and calls
`FUN_180045a50` to copy the probe. The copy selects the same ordinary-family
record-copy mode and preserves `c7`, but does not copy classifier buffer
`+0x158`. `FUN_18005d330` reuses one gallery-derived policy across its queue
scan, forces word 18 to zero, selects trigger-only mode, and dispatches both
types to `FUN_180055a40` and `FUN_1800469f0`. Thus their different word-17
values and subtype-dependent matcher descendants remain active during queued
rematching; the shared queue controller is not proof of equivalent actions.

## Enrollment Boundary

The WBF owner `FUN_18002d280` calls `enrolStartEx(&maximum_40, 0, 0, 0)` and
then copies the caller's signed 16-bit required count into session `+8`. It
does not select adaptive enrollment mode by subtype. `enrolAddImage` reaches
the common extractor in mode zero, builds anti-fake state with
`FUN_1800392f0`, and calls `FUN_180042c30`.

At `FUN_180042c30`, both types use the default relation-mode descriptor
`[template.word3,0,0]` and bypass the type-1/type-8 feature-status rejection.
Both reject count at or above capacity with `0x80000005` and zero records with
`0x80000006`, clearing the progress output. First and subsequent insertion
owners are `FUN_180042610` and `FUN_1800422e0`/`FUN_180041ac0`/`FUN_180042840`.
Successful insertion resets order to physical identity and invokes
`FUN_18007a040` then `FUN_180047550` only at exact physical capacity.

The separate adaptive-mode initializer `FUN_180041800` is not interchangeable:
decoded type 0 fails its accepted-type masks and returns `0x80000003`; type 12
selects its case-8 policy. This does not block type-0 WBF enrollment because
the WBF creation owner explicitly selects mode zero, which bypasses that helper.

Direct enrollment relations use `FUN_180041ac0 -> FUN_180078750`. The latter
does not receive or read subtype and bounds selected descriptor pairs to 31;
the caller scans a 42-entry initialized workspace. After affine fitting, an
edge is admitted when `(inliers>=7 && metric>=209 && overlap>=65)` or
`inliers>=11` or `(metric>=216 && inliers>=6)`. The same equation applies to
both subtypes. Anti-fake pair comparison is conditional on both owners being
present and both feature coverages `>40`; its Boolean success controls the
two pair-score updates, not enrollment admission.

## Graph And Normalization

`FUN_180042840` consumes the just-inserted feature, outer gallery, and a mutable
relation/progress descriptor: word zero is the new relation-row base, word one
is candidate count, the qword at `+8` is the candidate-index pointer, the qword
at `+0x10` is the overlap-mode descriptor, and word six is the overlap threshold.
`FUN_180042c30` supplies threshold 205 for both types, independently of subtype.
It partitions candidate indices by feature active marker `+0x110`, temporarily
replacing the descriptor's count/pointer with stack-local subsets. The descriptor
is an immediate-call workspace, not retained storage.

Already active neighbors select `FUN_180041f60`: choose the strongest direct
relation, compose/invert geometry through reference index `+0x87d8`, mark the
new feature active, and publish the reference affine. Without active neighbors
and without an established graph (`+0x87e4 == 0`), `FUN_180042180` requires
the strongest relation strength `>5` and `FUN_180071d40` result strictly above
descriptor word six to establish the first reference and mark both endpoints.

Inactive neighbors go to `FUN_180042ac0` when the graph or new feature is
inactive: score against the caller threshold and select the least uncovered
geometric area. Otherwise `FUN_180043110` activates neighbors only when
`FUN_180071d40 >208`, composes their reference relations, sets those relation
strengths to zero, and propagates through `FUN_1800411f0`/`FUN_180042ed0` using
the caller threshold. These owners read relation strengths, active markers,
affines and overlap inputs, not subtype or coating. `FUN_180046780` subtracts
active, non-status-five gallery footprints from the incoming feature's mask,
excluding the supplied feature index. Half-resolution dimensions/translations
come from outer word three; `FUN_180042840` scales its returned area by four
when that word is nonzero. Its final high-byte progress calculation uses the
same dimensions and relation-strength-at-least-six predicate for both types.

`FUN_18007a040` performs common graph closure on outer feature count `+0x1c`,
feature pointers `+0x28`, triangular row bases at feature `+0x114`, and 28-byte
relation entries at outer `+0x1b8`. Its optional second argument supplies a
virtual additional node's relation row; enrollment capacity finalization passes
null. It traverses positive-strength edges and composes/inverts affine paths.
For a nontrivial path, it replaces a direct strength below two, or an inferred
strength two whose remembered path strength is lower, with strength two and
the composed affine. It propagates active markers between physical endpoints
when either was active and reports physical inferred-edge publication. No
subtype or coating participates in traversal, path comparison, or mutation.

`FUN_180047550` returns `0x80000002` for a null object; both types take
`FUN_180047120` and return zero. That callee does not read subtype or coating.
It lazily constructs missing feature `+0x140` masks, clears overlap count
`+0x148`, and initializes uncovered area `+0x128` to working width times height.
For active features it composes reference-relative affines, subtracts other
active footprints, counts footprints with full-resolution area strictly above
40 percent, and stores remaining mask popcount (zero below 20). Half-resolution
mode halves dimensions and translations, and scales footprint area by four.
Inactive features keep the initialized working area. Temporary masks are freed;
feature masks and normalized scalar fields remain owned by the gallery.
These are shared graph/normalization operations over potentially different
extracted records, relations, bitmap inputs, and prior study state.

## Outer Serialization And Admission

`FUN_18003eaf0` writes the actual subtype as tag `0x98` at packed offset
`0x0f`, with its little-endian dword at `0x10`. Both types have identical
outer header layout and use the non-special-family reference-star relation
projection. The encoder carries live queue state/counter as tags `fa/fb`,
but does not serialize the twenty queued feature owners or ranks. It passes
actual subtype to feature encoder `FUN_180040a70`.

`FUN_180040700` selects both record limits as 150 for either type. With a
nonnull decode context, feature maximum is unsigned `min(40,*context)`;
with null context it preserves serialized live maximum `+0x20`. The
post-decoder `FUN_18003fef0` retains this subtype distinction, restores both
record limits, and applies the same ordinary-family negative-angle adjustment
and mode-zero per-record transform to types 0 and 12.

`identifytemplate` (`0x1800024f0`) does not compare the two template subtype
words. It shallow-copies the second outer object, iterates its features as
probes, and calls `FUN_18005edb0` using the first template as gallery/config.
The first positive score publishes its probe-feature index; exhaustion writes
`UINT32_MAX`. `FUN_18005edb0` likewise has no cross-template equality gate:
its live probe is a feature, not an outer object carrying subtype. These
boundaries do not establish an adapter-level cross-type admission policy.

`FUN_180040a70` selects the same 32-byte packed-record arm for types 0 and 12
using mask `0xc0000000006138cd`. It applies `FUN_18004c9f0` to a local record
copy, omits the same live-only spans, and writes the same scalar tags and
conditional `c7`. The shared feature and outer layouts preserve subtype in
the outer header rather than introducing a second record format for type 0.

`FUN_18003e3a0` restores tag `0x98` directly into live word zero, without
comparing it to current profile globals. Both types skip special-family graph
closure, then run common normalization `FUN_180047550` after reconciliation.

## Adapter Storage Identity

`FUN_18001e5b0 -> FUN_18001d3b0` requests the 48-byte sensor-info record with
`DeviceIoControl(0x442004)` into context `+0x48`. The helper requires exactly
48 returned bytes; failure maps to `0x80004005`. Pending I/O waits through
`GetOverlappedResult`. Context bytes `+0x4a..+0x59` are the sensor identity.
`FUN_180030c40` passes those bytes to `FUN_18002b240`, which copies exactly
16 bytes to global `0x18019b9b8`. The identity is externally supplied sensor
information, not constructed here from the template subtype or image geometry.

The [USB sensor-info producer](PROFILE0-USB-CONTRACT.md#sensor-info-identity-producer)
closes that identity's provenance: these are the first sixteen retained OTP
bytes. USB `FUN_1800222a0` exports the selected profile separately at response
`+0x28`; no chip ID or profile is hashed, appended, or compared inside the
sixteen-byte identity. Optional OTP replacement preserves those sixteen bytes,
and successful OTP normalization changes only later bytes. Equal storage
identity therefore does not establish equal profile or algorithm subtype.

`FUN_18002fcb0` copies this identity into the publication envelope at
`0x18019b9c8`; its callers are enrollment packing `FUN_18002d8b0` and study
publication `FUN_18002bbf0`. `FUN_18002e360` compares all 16 incoming record
prefix bytes before envelope validation and `templateUnPack`. Inequality sets
status `0x8011` and bypasses unpack/matching. This is a sensor-ID equality
contract, not an explicit inner subtype-equality contract.

The same attach owner takes enrollment required count from configuration byte
`+0x41e`, clamping values below 9 to 8, without a subtype test. Recognition
mode is one. Quality, coverage, and enrollment overlap bytes come from
configuration `+0x41f..+0x422`; anti-fake mode comes from `+0x423`. After
`identifyImage`, `FUN_18002e360` admits a nonnegative matched index and signed
score strictly greater than zero, without an additional numeric match
threshold or subtype branch.

The configuration image at `0x1800f141c..0x1800f1423` supplies standard count
8, coating count 12, Glass count 12, quality 25, coverage 65, overlap 85/75,
and anti-fake mode 1. `FUN_1800054e0(0)` returns the common configuration
object; the attach count source is specifically its Glass byte `+0x41e`, not
the standard or coating count. These are image defaults, not a claim that
external configuration cannot replace them.

Publication has no subtype test in `FUN_18002ba60` or `FUN_18002bbf0`.
The former calls `templateStudy` only for successful-match latch exactly one,
and asks for a new packed size only for signed action `>0`. The latter packs
only positive actions, then deletes every populated handle in the 200-entry
candidate array. Queued owners omitted by serialization therefore do not
survive this adapter transaction's handle cleanup for either subtype.

## Anti-Fake Feature Ownership

`FUN_1800392f0`, called by both `enrolAddImage` and `identifyImage`, allocates
or reuses feature `+0x160` and clears its complete `0x1abc` bytes. It decodes
subtype from chip-info bits 3..8 and forwards it to residual/filter/descriptor
owners. Both types use the caller's offset arithmetic
`int32((dac_high - dac_low) * tcode * 0x11b) / 1000` with wrapping dword
multiplication and signed truncating division.

Type zero has an explicit final packing branch: from each 108-byte dense-mask
row it packs columns 2..105 into a contiguous 104-bit row, for 9,152 bits
(1,144 bytes) across 88 rows. It subtracts two from the signed X dword of
every 48-byte anti-fake record. Type 12 instead packs all 108 columns, totaling
9,504 bits (1,188 bytes), and does not subtract two. Both first clear the entire
2,000-byte packed-mask field at anti-fake `+0x12dc`, so the unused suffix is
zero. Both call `FUN_18004b770` after this branch with the actual subtype and
publish pair score `-1` at `+0x1ab4` afterward.

The residual descendant `FUN_18003a150` forms each interior dword as
`calibration - raw - sensor_offset - 0x1bb7` and copies its low 16 bits.
Type 12 replicates the nearest interior row/column into the one-pixel border;
type 0 skips replication and retains the zero-initialized border. The next
helper `FUN_18003a9b0` does not consume its subtype argument, so filtering is
the same operation on these different residual inputs.

`FUN_18004b770` consumes the first 9,152 residual words sequentially under
the live 104x88 geometry for both types, not a per-row 108-to-104 crop. Both
use unsigned `min(residual/12,255)` and the same fixed descriptor configuration
whose first word is literal 12, independent of its incoming subtype. Type 0's
record X shift has already happened before this descriptor sampling. Shared
descriptor arithmetic therefore consumes different coordinate and residual
inputs.

`FUN_18003a3e0` receives two anti-fake objects, a six-dword Q8 affine, explicit
rows/columns, and a caller-zeroed five-dword result. The relevant call sites
`FUN_180041ac0:0x180041e6d` and `FUN_180055a40:0x18005670e` load rows from
outer `+8` and columns from outer `+4`: 88 and 104 for both types. They do not
substitute the type-12 packed-mask producer's width 108. The comparator neither
receives nor reads subtype/coating, and performs no layout conversion.

`FUN_18003c520` copies `ceil(rows*columns/8)` bytes of the first packed mask,
inverse-maps each set bit, and clears it when the corresponding second bit is
out of bounds or unset. Thus comparison interprets both masks as contiguous
104-bit rows, including type 12's upstream 108-column-packed bytes. It then
forward-maps second-object integer record coordinates into that intersection,
finds reciprocal nearest records under inclusive unsigned squared Q8-distance
limit `0x400`, and derives support ratios and lower-median descriptor distance.
First-object record coordinates are assumed valid when indexing their support
bits; the first-side loop does not revalidate bounds. The complete arithmetic,
tie ordering and output equations belong to `functions/FUN_18003a3e0.md`.

The result is pair presence, not match acceptance. Zero pairs always writes
result word one zero, leaves words two through four at their caller defaults,
and returns zero. With pairs it writes words one through four and returns one;
word zero is never written. Both objects and their boundary score `+0x12d0`
are untouched. Enrollment uses pair presence to update endpoint pair scores;
the matcher's exact-type-12 rejection ladder remains a separate policy owner.
Consequently the comparator is shared, but packed-mask geometry, record X
coordinates, residual-derived descriptors, and downstream rejection are not
equivalent across the two types.

## Descriptor Descendants

For both subtypes, `FUN_18004ac40` selects six levels, and `FUN_18004c100`
scans three scale positions with the same six-pixel border. Its exceptional
recovery condition `height*5 < width*2` is false for 104x88. Both select the
ordinary `FUN_18004c5a0` refinement branch with scale divisor 3, and
`FUN_18004bbb0` emits one orientation rather than the special-family multiple-
orientation route. `FUN_18004cc70 -> FUN_180048980` selects the same descriptor
encoders for types 0 and 12; `FUN_180081be0` consumes descriptor length/stride,
not subtype. These shared descendants preserve the earlier record-budget,
retry, validity-filter, metadata, and anti-fake differences.

## Comparison Boundary

Shared in the listed descendants means the same native operation for the same
explicit inputs, not equivalent full pipelines. Secondary selectors preserve
the 31/42 pair budget, rescue preserves its 31/42 score denominator, and the
shared anti-fake comparison consumes differently packed support and shifted
record coordinates. Graph propagation and normalization use shared policy but
consume the relations and masks actually produced upstream.

Coating has no direct selector in these secondary geometry, score/rescue,
graph/normalization, or anti-fake comparison owners. Its upstream effects belong
to `PROFILE0-ENGINE-CONTRACT.md`. Adapter storage admission remains the
16-byte sensor-identity comparison, not an inner template-subtype comparison;
the sensor-info identity producer below `FUN_18001d3b0` supplies the first sixteen
retained OTP bytes, as established in `PROFILE0-USB-CONTRACT.md`. Neither shared
serialization nor these shared
kernels establishes cross-device enrollment compatibility.
