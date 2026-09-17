# identifyImage

## Binary And Body

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `identifyImage`, `0x180001ad0..0x180001f6e`.
- Role: exported image-to-feature and candidate-matching entry point.

## Call Graph

- Adapter callers include `FUN_18002e360` and `FUN_1800303c0`.
- Relevant callees are `FUN_18004ae70` for live-probe extraction,
  `FUN_1800392f0` for anti-fake data, and `FUN_18005edb0` for matching.

## Production Geometry Path

The export validates and copies the supplied processed image, calls
`FUN_18004ae70` to create the live probe object, then supplies that object to
`FUN_18005edb0`. Profile initialization remains visible through the
`DAT_180218e*` globals used for anti-fake chip information. The exported image
path does not replace the probe object's dimensions after feature extraction.

For profile 9, the input processed image is the full
`108x88` 9,504-byte frame. `FUN_18004ae70` performs the subtype-12 matcher
normalization to `104x88`; that normalized descriptor, not the raw image header,
is what later reaches `FUN_180058700`.

At `0x180001c5a..0x180001c8a`, the extractor receives quality and coverage from
the processed descriptor, constant extraction selector 1, the first candidate's
internal profile/configuration object, and the export's auxiliary pointer. That
auxiliary pointer is the preprocessor workspace in the normal API lifecycle; it
is not candidate state or the processed image buffer. The extractor decodes its
first three bytes through `FUN_180073830` for the later classification config.

Only the first candidate supplies extraction configuration. For ordinary
profile-9/type-12 enrollment, `FUN_180037c80` initializes its first seven dwords
to `[12,104,88,1,1,150,150]`. `FUN_18004ae70` reads these configuration words,
not gallery current-count `+0x1c` or maximum-feature-count `+0x20`. In identify
mode, word 6 supplies the live record limit before coverage scaling. Different
enrolled-feature counts therefore do not themselves change the first
candidate's extraction configuration. See `FUN_180037c80.md` and
`FUN_18004ae70.md`.

## Array ABI And Retained Owners

- Live extraction call and retained probe object: `0x180001c8a` calls
  `FUN_18004ae70` before the candidate loop.
- Matching dispatch: `0x180001e32` calls `FUN_18005edb0`.
- Candidate-array ABI: the export requires `candidates[0]` and
  `candidates[0]->pFingerTemplate` to be nonnull, then uses the internal pointer
  as the matcher candidate. `FUN_18002e360` supplies the array of public handles,
  and `FUN_1800303c0` independently uses the address of an `enrolGetTemplate`
  handle as its one-candidate array.
- On a positive score, the export stores the matched internal template at
  `0x18024e548`; the retained live probe remains at `0x18024ebe8` for the
  immediately following `templateStudy` call.

## Pre-Match Construction

At `0x180001dbd`, after extraction and metadata setup, global `0x18024ebe8`
owns the `0x168` live feature and its active 56-byte records. Feature matrix
`+0x20` is the pre-filter `52x44` owner built before optional broken-pixel
clearing; it supplies the later anti-fake mask. Rescue matrix `+0x140` is a
different owner derived from finalized inline mask `+0x28` and consumed by
`FUN_18005d9e0`.

These matrix owners use a `0x20`-byte header: width, height, stride, payload byte
count and element size occupy `+0x00..+0x10`, and the payload pointer is at
`+0x18`. `FUN_180068700` initializes those fields but does not initialize the
alignment gap at `+0x14..+0x17`. That gap is not matrix shape or pixel state.

The status test at `0x180001c96` sends nonzero extraction status to the
`0x80000001` return at `0x180001cb2`. Status zero falls through metadata setup
and `0x180001dbd` without a conditional bypass, then calls `FUN_1800392f0` at
`0x180001dd3`. Low coverage does not introduce a second successful route around
this construction boundary. Preprocessing rejection occurs before this export
and is a separate status boundary.

## Retained-Probe Serialization Window

The probe-global address has code references only in this export and
`templateStudy`: extraction receives its address at `0x180001c65`, anti-fake
and matching read it at `0x180001dbd` and `0x180001e20`, and study reads or
destroys it. This export resets the matched-gallery global, not the probe
global. A completed no-match therefore retains the allocated probe for a later
image extraction. It is incorrect to infer a fresh feature allocation merely
from the start of another identify call. Normal study with a valid retained
winner destroys the probe through `FUN_180037b10` and clears its owner; the
following extraction then allocates a new feature.

### Ordinary Probe Residual Invariant

The retained image probe's `+0x128` (serialized `bb`) stays zero throughout its
ordinary profile-9 lifetime. The first extraction allocates and clears all
`0x168` bytes. Later extraction into the same object preserves that dword;
`FUN_18004ae70` does not initialize it again. `FUN_1800392f0` only installs the
separate `+0x160` owner and fills that anti-fake allocation, so it does not write
the feature residual. Type-12 dispatch and matching keep the probe separate
from their writable gallery, queue, candidate and evidence owners; see the
cross-gallery ownership contract in `FUN_180055a40.md`.

This gives an induction across nonpositive gallery comparisons and repeated
no-match image calls: the retained probe enters and leaves each extraction and
match with `bb=0`. A successful study can mutate gallery features and separate
queued copies, then destroys this probe and clears the global; the next probe
again starts allocation-zero. Rejection before extraction leaves the existing
probe unchanged. An empty extracted record array still has this zero residual;
its later matcher error does not install a gallery residual into it.

No ordinary exported path imports a packed feature into the probe-global owner.
`identifytemplate` has its own supplied feature boundary. The gallery residual
writers (`FUN_180047120`, append `FUN_1800456b0`, and copy `FUN_180045a50`) do
not receive this retained image probe as their destination. In particular,
normalization's `2288` initialization belongs to gallery features, not this
image probe. Literal serialization of the retained image probe therefore
emits `bb=0` both after fresh extraction and after reuse. This invariant does
not apply to a feature pointer supplied directly to internal helpers or to a
separately unpacked/normalized template feature.

`FUN_180040a70:0x180041055` serializes the residual literally; packing does not
normalize it. A separately normalized feature used as an internal probe can
therefore have a different serialized `bb` from this image producer. Probe
residual is not selector policy input: `FUN_180045530` reads residuals only
through the gallery feature array, and computes uncovered probe support from
the probe mask through `FUN_180046780`. Queue copy clears its destination
residual, append initializes the new gallery residual, and replacement keeps
the target residual until action-specific normalization. These ownership rules
prevent an arbitrary probe residual from becoming the gallery residual; see
`FUN_180045a50.md` and `FUN_18005d330.md`.

### Complete Serialization Timing

The pre-anti-fake boundary at `0x180001dbd` is too early to serialize the raw
one-feature probe template consumed by the native runtime. On first allocation
live feature `+0x160` is null; after a preceding no-match it can retain the
previous image's anti-fake allocation. Extraction does not clear that owner.
`FUN_1800392f0` allocates the `0x1abc` object when absent and unconditionally
clears and populates it for the current image. The completed object is
observable at `0x180001dd8`, immediately after the call, and remains owned by
global `0x18024ebe8` after `identifyImage` returns.

The post-return/pre-study window contains the complete retained probe.
`templateStudy` consumes, destroys, and clears it. Serializing before anti-fake
construction would instead encode an absent or previous-image anti-fake block.

## Canonical One-Feature Probe Projection

The extraction call receives `&0x18024ebe8` as its output owner at
`0x180001c65`, and later anti-fake and matcher calls read the resulting pointer
at `0x180001dbd` and `0x180001e20`. `identifyImage` does not destroy it on a
normal return. `templateStudy` consumes and clears it through `FUN_180037b10`,
so post-return/pre-study is the complete ownership window for exact probe
serialization.

The live feature can be projected into a one-feature template independently of
the DLL's outer gallery packer.
The one-feature projection has a fixed 1,433-byte contribution around the
feature element:

```text
10-byte 87/CRC/86/payload envelope
13 tagged header dwords (65 bytes)
one 95 feature element
93 graph block (25 bytes): f2=f3=f4=UINT32_MAX, f5=0
94 tail block (1333 bytes including tag/length; payload length 0x530)
```

There are no relation elements. The 13 header tag/value pairs are:

```text
81=0x11f248ea 98=12 9a=88 9b=104 91=1 97=1 92=1
9e=150 9f=150 9c=1 9d=1 fa=0 fb=0
```

The fresh tail is deterministic: the first 200 bytes are `ff` except dword zero
is zero, dword `+0xc8` is `UINT32_MAX`, `+0xcc` starts with the NUL-terminated
string `Milan_v_3.01.09.10.50`, and the remaining reserved/counter bytes are
zero. The envelope payload length and CRC are backfilled after serialization;
their wider template semantics are outside this feature audit.

Combining the fixed outer bytes with the type-12 feature contract gives exact
probe size:

```text
9378 + 32 * record_count + (c7 != 0 ? 5 : 0)
```

This projection does not contain the live classification owner `+0x158` or all
56-byte record fields. Equality of packed projections alone is therefore weaker
than equality of the complete inputs used by successive matcher calls.

The live owner is not limited to three-byte modes 0..2. Ordinary selector-zero
profile-9 producers can reach modes 3, 4, and 5 after three successful retained
plane appends. In particular, a broad primary histogram can produce seed 1;
a stable class-1 count above 600 pixels then promotes it to mode 4. Mode 4
is not excluded by sensor type 12 or this export. Modes 3/4 own the complete
2,288-byte history projection; mode 5 owns the complete projection of the
same-call auxiliary decision plane. These buffers include the final summary
overwrite at bytes 0..2. The preprocessor seed, promoted live mode, and packed
`c7` high class are distinct values; none can substitute for the complete live
owner when comparing successive candidate calls. See
[`FUN_180048260`](FUN_180048260.md) for the producer transitions and strides.

## Outer Candidate Selection

The candidate loop at `0x180001df0..0x180001ef1` evaluates handles in supplied
order and returns immediately on the first positive matcher score. It does not
evaluate later candidates to maximize score. If every candidate is nonpositive,
the published index remains `UINT32_MAX` and the published score is the final
candidate's score.

The public matched index is only the zero-based position in the supplied handle
array. The DLL has no caller gallery-index field and does not translate the
position through external metadata. Duplicate caller-side indexes therefore
cannot affect this export; that distinction belongs to the native runtime
wrapper.

The adapter validates all top-level output pointers at
`0x180001b3b..0x180001b62`, then initializes published score to zero and index to
`UINT32_MAX` at `0x180001b68..0x180001b6c`. Candidate count is an unsigned
32-bit value. Count zero returns `GF_NO_Candidate` (`0x81`) before entering the
loop and leaves those initialized outputs at `0/UINT32_MAX`; it is not an
empty-list invocation of the selection loop. The first candidate and its
template pointer are checked before extraction. For later entries the loop
dereferences the public handle first, then checks its internal template pointer.
A null internal template returns `0x81` when reached; a null later public handle
is not protected by that check. Even count zero is tested only after the first
handle and its internal pointer have been read and validated.

For each successful matcher call, `0x180001e87..0x180001ea3` tests the signed
score with `JG`. Negative and zero scores continue; a positive score publishes
that exact dword and the current zero-based index at
`0x180001ec8..0x180001ed3`. On loop exhaustion,
`0x180001ea9..0x180001ec3` publishes the final matcher-written score and resets
the index to `UINT32_MAX`. A matcher error aborts with `0x83` rather than
participating as a score.

Each successful matcher call operates on the candidate object reached at that
position. Its normal match-time mutation is not rolled back when the score is
nonpositive. On a positive score, `0x180001ed3` retains exactly that mutated
candidate in global `0x18024e548`; later candidates are untouched. The live
probe at `0x18024ebe8` is shared by the ordered loop. `templateStudy` receives
the retained winner and probe, then consumes the probe. Entry clears the retained
winner, so an all-nonpositive call leaves no candidate eligible for study.

The array borrows handles; it does not clone, deduplicate, or restore their
internal templates. Independent per-row ownership therefore requires distinct
internal template objects, not merely distinct array slots. If two supplied
handles alias one internal template, the later occurrence sees the earlier
occurrence's gallery and queue mutations. Repeated packed bytes unpacked into
separate handles do not have that aliasing behavior.

The loop counter and count cannot wrap during a valid traversal: after index
`count-1`, increment produces `count` and the unsigned-below branch stops.
Native trusts that the caller's candidate pointer array has `count` entries;
an overstated count is a malformed out-of-bounds/crash surface, not a score
policy.

Immediately before each matcher call, `0x180001e27` restores the operation's
anti-fake mode at shared evidence `+0x690`. The dispatcher builds fresh policy
from that row, including packed probe class; the type-12 matcher saves the seed
and clears/reinitializes the remaining evidence. Its accumulated gallery classes,
late-status counter, affine workspaces and candidate flags are invocation-local.
These mutable values are not written back to the probe's packed class or live
classification owner. See `FUN_18005edb0.md` and the cross-gallery ownership
contract in `FUN_180055a40.md`.

The clean-room runtime's index/position mapping, invalid-row handling,
per-candidate queue ownership, study handoff, and parity aggregation contract are
documented in `re/milan/RUNTIME-IDENTIFY-ARBITRATION.md`.
