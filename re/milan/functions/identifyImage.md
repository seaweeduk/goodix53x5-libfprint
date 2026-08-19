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

For the authoritative profile-9 runs, the input processed artifact is the full
`108x88` 9,504-byte frame. `FUN_18004ae70` performs the subtype-12 matcher
normalization to `104x88`; that normalized descriptor, not the raw image header,
is what later reaches `FUN_180058700`.

At `0x180001c5a..0x180001c8a`, the extractor receives quality and coverage from
the processed descriptor, constant extraction selector 1, the first candidate's
internal profile/configuration object, and the export's auxiliary pointer. That
auxiliary pointer is the preprocessor workspace in the normal API lifecycle; it
is not candidate state or the processed image buffer. The extractor decodes its
first three bytes through `FUN_180073830` for the later classification config.

## Evidence And Confidence

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
- Confidence is high for the exported production route and distinction between
  the raw `108x88` frame and the normalized match object.
- Unresolved ABI names for unrelated exported arguments do not affect geometry.

## Safe-Zero Policy Boundary

The v2 study authority supplies a DLL-unpacked native safe-zero probe at global
`0x18024ebe8`, replaces only the extraction call at `0x180001c8a` with a
successful no-op, and replaces only the anti-fake rebuild call at `0x180001dd3`
with a no-op. The candidate loop from `0x180001df0`, `FUN_18005edb0`, global
evidence at `0x18024e550`, positive winner publication at `0x180001ed3`, and the
subsequent `templateStudy` call remain official DLL code. This is an oracle-only
input bridge, not native runtime behavior and not a claim that patched Windows
execution is the production Windows byte path.

## Controlled-Boundary Intervention Point

The replacement normal-extraction oracle intervenes at `0x180001dbd`, after
the successful `FUN_18004ae70` return and all metadata calculations, but before
the argument loads and untouched call to `FUN_1800392f0` at `0x180001dd3`.
At that point owning global `0x18024ebe8` contains the normal `0x168` live
feature and its active 56-byte records. Replacing only feature matrix `+0x20`
with an allocator-compatible shadow does not bypass extraction, anti-fake
construction, candidate matching, evidence publication, or ownership cleanup.
This matrix is the pre-filter `52x44` owner built before optional broken-pixel
clearing and is the later anti-fake mask input. It is independent of rescue
matrix `+0x140`, which extraction derives from finalized inline mask `+0x28` and
which `FUN_18005d9e0` consumes during aggregate rescue.

The intervention resumes the original instruction at `0x180001dbd` through a
single-step breakpoint rearm. It does not alter the extraction call at
`0x180001c8a`, the anti-fake call at `0x180001dd3`, or the matcher call at
`0x180001e32`.

There is no successful-extraction branch around this intervention point. The
status test at `0x180001c96` sends nonzero extraction status directly to the
`0x80000001` return at `0x180001cb2`; status zero falls through metadata setup
and the logging calls to `0x180001dbd`. No conditional branch occurs between
the successful status test and that instruction. Consequently, an
`identifyImage` status-zero result with a retained serializable probe must have
crossed the pre-anti-fake boundary exactly once. A zero boundary-hit count is
not a valid low-coverage success path.

A focused low-coverage diagnostic supplied an already sealed processed frame
with quality 28 and coverage 14 directly to `identifyImage`. The DLL produced a
serializable 20-record probe and two valid no-match gallery rows, and the
`0x180001dbd` breakpoint fired once. This independently confirms the static
control flow. The exported preprocessor rejected the corresponding raw-frame
call earlier with `0x29aa`; that separate preprocessing rejection must not be
misclassified as a skipped identify boundary.

## Retained-Probe Serialization Window

The pre-anti-fake boundary at `0x180001dbd` is too early to serialize the raw
one-feature probe template consumed by the native runtime. At this point live
feature `+0x160` is normally null. `FUN_1800392f0` begins by allocating the
`0x1abc` anti-fake object at that field when absent, then clears and populates
it. The completed object is observable at `0x180001dd8`, immediately after the
call, and remains owned by global `0x18024ebe8` after `identifyImage` returns.

The narrow non-handler serialization window is therefore immediately after
`identifyImage` returns and before `templateStudy`. The latter consumes,
destroys, and clears the retained feature. The `0x180001dbd` exception handler
should remain limited to the allocator-compatible matrix shadow; invoking the
template packer there would encode an absent anti-fake block and add complex
DLL work while execution is interrupted.

## Sequence-2 Builder Boundary

A focused passive breakpoint at `0x180001dd8`, immediately after the untouched
anti-fake call and before the candidate loop, captured the live probe's complete
anti-fake block. It is byte-identical to the existing post-return snapshot,
SHA-256
`7bb448a5159838c3218820f2e4c62ef7626b52789a412ab4ac0453c3726ba354`,
and already contains boundary score `+0x12d0 = 13`. Matching does not introduce
the value. The temporary breakpoint instrumentation was removed after capture;
only derived artifacts and this provenance note remain.

## Outer Candidate Selection

The candidate loop at `0x180001df0..0x180001ef1` evaluates handles in supplied
order and returns immediately on the first positive matcher score. It does not
evaluate later candidates to maximize score. If every candidate is nonpositive,
the published index remains `UINT32_MAX` and the published score is the final
candidate's score.

The adapter validates all top-level output pointers at
`0x180001b3b..0x180001b62`, then initializes published score to zero and index to
`UINT32_MAX` at `0x180001b68..0x180001b6c`. Candidate count is an unsigned
32-bit value. Count zero returns `GF_NO_Candidate` (`0x81`) before entering the
loop and leaves those initialized outputs at `0/UINT32_MAX`; it is not an
empty-list invocation of the selection loop. The first candidate and its
template pointer are checked before preprocessing, and each later null candidate
returns `0x81` when reached.

For each successful matcher call, `0x180001e87..0x180001ea3` tests the signed
score with `JG`. Negative and zero scores continue; a positive score publishes
that exact dword and the current zero-based index at
`0x180001ec8..0x180001ed3`. On loop exhaustion,
`0x180001ea9..0x180001ec3` publishes the final matcher-written score and resets
the index to `UINT32_MAX`. A matcher error aborts with `0x83` rather than
participating as a score.

The loop counter and count cannot wrap during a valid traversal: after index
`count-1`, increment produces `count` and the unsigned-below branch stops.
Native trusts that the caller's candidate pointer array has `count` entries;
an overstated count is a malformed out-of-bounds/crash surface, not a score
policy. The public clean-room helper rejects a null score array, zero count, or
null outputs with `-1` and leaves outputs untouched, so its empty-list behavior
is intentionally safer than the adapter's pre-loop `0x81` plus initialized
outputs.

The native helper `goodix_milan_match_select_first_positive()` models this
contract for valid nonempty score arrays, and the native Milan runtime invokes
equivalent first-positive behavior. Publication order and signed-positive
selection already match and require no production change; only the helper's
explicit fail-closed API validation differs from the adapter's fault/status
surface.
Every native live probe reaches this loop after complete anti-fake construction
with semantic zero at the documented one-past source. Zero/nonzero differential
construction is offline diagnostics only and cannot stop verification,
identification, matching, or study.
