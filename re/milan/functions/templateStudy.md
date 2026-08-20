# templateStudy

## Binary And Body

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `templateStudy`, `0x180001f70..0x180002094`.
- Role: consume the retained successful image-identification state and run the
  official study/update policy.

## Inputs, Ownership, And Behavior

- Matched enrolled template: global `0x18024e548`.
- Retained live probe: owning global `0x18024ebe8`.
- Recognition evidence: global `0x18024e550`, size `0x698`.
- Study is dispatched through `FUN_180044fc0(..., mode=1)` only when the matched
  template and gate at `0x18024ebd8` are nonzero.
- The call at `0x180001ff7..0x180002006` passes that evidence base unchanged.
  Native study consumes relation affine `+0x66c` but not strict matcher count
  `+0x668`. Current source represents the handoff as a seven-word adapter whose
  word zero is a required zero sentinel and whose remaining six words are the
  affine; its separate `relation_count` corresponds to native `+0x668`.
- The returned update code is written to the caller. The retained probe is then
  destroyed and the owning global cleared through `FUN_180037b10` on every
  normal or error exit; the probe is single-use.
- On the production type-12 accepted path, append installs one new order entry,
  queued follow-up handling completes, and `FUN_180044fc0` calls
  `FUN_1800607b0` before the updated live template is packed. That helper owns
  the defined gallery order transition; it is not probe or relation state.

## V2 Authority Consequence

`identifytemplate` cannot prepare this contract because it uses stack-local
evidence and publishes none of these globals. A defined safe-zero policy oracle
must instead let `identifyImage` run its official candidate matcher against the
DLL-unpacked native probe, then call this export immediately. Replacing only the
image extractor and anti-fake rebuild calls before matching preserves the
official evidence, matched-template, study, ownership, and packing behavior
while excluding `FUN_1800392f0`'s undefined live feature-matrix read.

## Confidence And Unresolved

- Confidence is high from decompile, assembly, and global xrefs.
- The earlier 798-row chronology emitted only `0/1/4`. The current strict
  643-operation authority emits `56` action-1, `5` action-2, and `442` action-4
  candidates. Code 3 remains naturally unobserved. Static assembly and direct
  controls establish its complete mutation, export, packing, and positive-
  persistence behavior; this proves semantics, not natural frequency or real-
  hardware reachability.

## Native Verification

The native type-12 append and replacement pack paths now run the recovered
`FUN_1800607b0` order finalizer after feature mutation and before serialization.
The first achievable-v2 append transition is byte-exact, including the 200-byte
tag-`a1` vector and downstream CRC, at SHA-256
`ef9805bf57ae44f3d4f690517ee1973706e707396a2161502f700360de776bf4`.
This changes no feature mutation, action selection, persistence rule, or runtime
wiring.

The focused sequence-3 lifecycle trace further proves that match-time `be`
increments precede this export and belong to the matched gallery object retained
by `identifyImage`, not to `templateStudy`. The public handle owns that same
DLL-unpacked object, and global `0x18024e548` passes it into study without an
intervening unpack/copy. The separately owned probe at `0x18024ebe8` remains
single-use. For sequence 3, update code `1` causes the complete after-study object
to be packed and carried as both `next-persistent.bin` and sequence 4's
`loaded-before-match.bin`; the after-match snapshot alone is not persisted.

The native sequence-3 probe-bridge correlation adds an ownership consequence:
native study already consumes the immutable serialized probe owned by
`GoodixMatchInfo`, while native matching currently consumes a duplicated live
record array whose mode-1 derived descriptor spans do not survive serialization.
For the frozen sequence-3 input, matching that owned serialized object produces
the official direct-positive mask `0x18f`; matching the duplicate decomposed
records produces `0x187`. The narrow recommended boundary is therefore to make
the serialized probe authoritative for both native matching and study, subject
to full matcher/rescue/study/hardware gates, rather than expand the duplicate
`GoodixMatchInfo` field set. This is a native ownership recommendation only and
does not change the DLL's proven single-use retained-probe lifecycle.

The native ownership recommendation has now been implemented at the two match
wrappers. It closes sequence-3 match-time parity, including score 41, direct mask
`0x18f`, and the exact after-match object. The complete immutable matcher gate,
however, regresses from score/decision/index `798/798` to `618/795/795`. The
implementation therefore remains unaccepted pending a focused cross-row split
of packed-record canonicalization and dense-versus-inline rescue derivation.
Study ownership and mutation were not changed, and post-study sequence-3 work
must wait for that matcher-level contradiction.

## Controlled-Boundary Methodology Correction

The v2 authority consequence and serialized-probe ownership recommendation
above are historical and rejected. `templateUnPack` reconstructs 32-byte packed
records and cannot replace normal `FUN_18004ae70` live extraction with active
56-byte records. The production wrappers have returned to normal live inputs.

The approved immutable prerequisite instead lets `identifyImage` execute normal
extraction and `FUN_1800392f0`, controlling only matrix source index 2,288 with
the DLL-owned shadow documented in `identifyImage.md` and
`FUN_1800392f0.md`. That replacement is score/decision/outer-index exact
`798/798` and leaves evidence and after-match template bytes unchanged under
paired sentinel controls. A new chronological study freeze may now call this
export after that normal identify path; existing v1/v2 chronological trees
remain observations, not authority.

## Retained-Feature Activation Boundary

The corrected normal-boundary chronological reference proves that `b5` study
activation occurs inside this export after the exact after-match snapshot and
before append. `FUN_180044fc0` gates `FUN_180046430` on retained evidence count
`+0x640 > 0` and flag `+0x644 == 1`; the helper sets live feature `+0x110`
(packed tag `b5`) to one for every indexed evidence entry. Append then copies the
matched feature's post-refresh `b5` to the new feature. The type-12 order
finalizer reads those values later, and packing only serializes them.

On enrollment 1 sequence 3, retained entries `[8,7,2,1]` activate existing
features `{1,2,7,8}` and matched feature 1 causes appended feature 9 to inherit
one. The complete existing-plus-new affected set is `{1,2,7,8,9}`. Loaded and
after-match objects are already native/official exact, so this transition is
strictly study-time. Positive update `1` then carries the complete after-study
object to `next-persistent` and the next row's loaded object.

The native wrapper now preserves that matcher-owned ordered list and flag,
validates the fixed capacity and indices, and performs only this pre-action `b5`
refresh. Focused corrected-state controls cover sequences 2/3/4/6 and enrollment
2 sequence 5, including selected-only and all-direct-positive negative rules.
Sequence 3 has exact native/official `b5` vector
`[1,1,1,0,1,0,0,1,1,1]` and exact order
`[8,9,7,2,1,0,4,5,6,3]`. The bounded native matrix now also produces exact
reference-star slots `[1,2,7,22,29,37]`, relation count six, and the complete
defined after-study transition. Persistence and update policy were not changed.

## Live Relation Boundary

The relation-count boundary is now recovered without changing production code.
The matched template remains a live matrix through study. Retained refresh
materializes zero-leading reference edges for entries `[8,7,2,1]`, and append
materializes one more edge because matched feature 1 is active. With the prior
positive slot 7, the serializer projects reference-0 slots
`[1,2,7,22,29,37]`, producing six records. Native emits one because it carries
only slot 7 and has not implemented the retained/appended live matrix updates.

Across both authoritative lanes, 64 appends have relation-count deltas zero once,
one 60 times, two once, four once, and five once; the larger deltas are exactly
retained-refresh plus append materialization. All 286 code-4 replacements
preserve packed relation cardinality and change exactly one star slot. Evidence
relation count ranges independently and is never an append or packing predicate.
Accepted update-0 controls in both lanes leave the complete star unchanged;
that earlier chronology did not exercise update code 2. The current strict
643-operation authority contains five natural action-2 candidates. A
nonreference witness clears every incident target slot and writes target `b5=0`
without reinstalling a reference edge; exact hashes and changed slots are in
`../PROFILE9-TYPE12-STUDY-RELATIONS.md`.

Native implementation reconstructs the 1,225-slot matrix from packed `e3` and
validated `b6`, carries retained transforms, applies refresh/append/code-4
mutations, runs the type-12 order finalizer, projects once in feature-index
order, and packs once. The former evidence-relation-count append switch and test
are removed. The complete action selector now carries matcher `+0x688`, closes
sequence-5 updates to enrollment-1 `0/0` and enrollment-2 `1/1`, and projects
all update codes exact `798/798`. Internal code 3 is exposed but discarded
without persistence. One bounded chronological replay next diverges at
enrollment-1 sequence 16 relation 19 transform word 1, native/official
`249/243`; it was not chased.

## Code-2/Code-3 Export Correction

Focused Ghidra and direct export controls on 2026-07-26 supersede the earlier
claim that internal code 3 is outside the external persistence contract.
`FUN_1800469f0` assigns 3 for a successful flag-one geometric fallback, performs
the replacement and relation mutation, and returns its selected index.
`FUN_180044fc0` preserves action 3 unless queued follow-up processing reports
action 5; with a disabled queue and with an enabled empty queue it writes 3 to
the result unchanged. This export then writes update 3 unchanged and returns
status zero.

Direct controls cover reference/nonreference targets at 80% and an aged 60%
reference target. In every case this export preserves the matched gallery
object, destroys and clears the single-use probe, and leaves the public gallery
handle packable. Production `_CheckUpdateTemplate_E` receives 3, embeds the
exact post-study packed object, and returns 3. The outer storage worker treats
every value `>=1` as positive. Code 3 is therefore exposed and persistence-
positive; it is not suppressed, mapped to 4, or retried as the same fallback.

Code 2 is likewise exported unchanged. Matched-primary, ranked-primary, and
matched geometric-fallback controls prove complete replacement, `b5/be/bc`,
relation, order, packing, and positive adapter handling. The inclusive boundary
is `matched_coverage-10`; `-11` returns update 0 and the adapter discards the
transient live object even though accepted-evidence order generation still
increments before the export.

Persistent evidence is under
`<evidence-root>/derived/milan-native-capture-20260723/study-action-code2-code3-v1/`.

Native now commits and packs code 3 instead of discarding it. The six positive
code-2/code-3 packed results are byte-exact to that ledger, the wrapper returns
the same positive action and payload, and failed packing publishes no updated
template. The subsequent queue recovery resolves supersession to 5 as an
intra-match deep-owned 20-entry contract. Current runtime executes the real
queued mutation and never fakes a 3-to-5 mapping.

## Error And Rollback Boundary

The null-output branch at `0x180001f88..0x180001fa4` attempts
`mov dword ptr [rbx],0` while `rbx==0` before its nominal `0x81` return and probe
cleanup. It is therefore an access violation, not the decompiler-implied safe
bad-parameter return. On ordinary paths the dispatcher always returns zero and
mutation helpers provide no transactional rollback. The export always destroys
the retained probe, but preserves the mutated gallery for caller packing.

## Exported Action 5

The prior queue blocker is closed. During one retained-probe transaction,
`FUN_180044fc0` can commit a primary action, re-match one or more queued deep
copies against that updated gallery, commit each positive queued action, and
replace the exported action with 5. The export still destroys the single-use
retained probe and leaves the same gallery handle packable.

Controlled profile-9/type-12 append-follow-up calls return status 0/update 5.
One entry produces feature count `9 -> 11`; two entries produce `9 -> 12` and
traverse physical slots `0,5`. Production adapter packing returns 5 and embeds
the exact packed gallery in both cases. Empty and queued-no-match controls
preserve primary action 1.

The authoritative `20260723` sequence-3 live-input control runs normal
extraction (115 active 56-byte records) and untouched identify (`0/37`),
deep-enqueues that retained live probe, and then calls this untouched export.
It returns status/update `0/5`, consumes the one queue entry, and packs
after-study/next-persistent SHA-256
`c971239885adccdcaece80b31dc9e5e3abea6b0cc428b1b82307cb05fe12b181`
identically in two fresh processes.

Queue entries do not persist across packing or adapter transactions. The
reachable producer is inside the same matcher call after aggregate rescue and
before normal aggregate/fallback publication. An achievable native
implementation therefore needs only a per-match deep-owned queue, not Windows
service state or a durable format extension. The producer sees rescue-owned
rejection evidence after the matcher's conditional `0x180056cba` clear.

The focused 2026-08-20 identify-376 state-zero witness distinguishes this
handoff without injecting a queue body. The approved DLL's strong rescue leaves
the empty queue at occupancy `0/0/0`, so primary action 4 remains externally 4
and the final candidate is
`e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350`.
Base commit `c182b140` freezes queue eligibility before rescue, enqueues the
retained probe after match (`0/1`), and this export's otherwise-correct consumer
drains it to zero, overrides 4 with 5, and publishes
`2a8be296bd58df5d5cf7ef2e8335d5506603f44b93334b81548a0cfb748bbcd5`.
The DLL/current state-1 control remains exact at action 4 and occupancy `0/0/0`.
Thus the action-5 consumer contract remains correct; the divergence is the
generic matcher producer's pre-rescue snapshot.

The adjacent retention-gate-zero control distinguishes producer evidence from
queue occupancy. With live probe `c0=2`, the DLL clears rescue rejection,
enqueues one entry during matching, and publishes action evidence zero. This
export therefore skips `FUN_180044fc0`, returns update 0, and leaves occupancy
`0/1/1`; after-match and after-study are both
`b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83`,
with no candidate. The discarded raw rescue-suppression patch instead removes
the entry while retaining action evidence, producing action 4 and candidate
`e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350`.
This is a matcher evidence defect, not a queue-consumer defect. Nonzero `c0` is
a DLL-generality discriminator only: the current matcher entry does not
transport it and ordinary extraction supplies zero. The working-tree production
correction therefore implements the shared evidence and continuation ownership
for the driver-reachable `c0=0` boundary without claiming the DLL's general
`FUN_18005e480` behavior.

## Native Action-5 Verification

The opt-in native queued study wrapper carries one caller-owned queue from the
detailed matcher, commits the primary and queued ordinary actions, validates the
final pack, and returns 5 only after a queued selector succeeds. Empty and
queued-no-match cases preserve the primary action. Queue state is destroyed by
the caller and is absent from the returned template; legacy no-queue callers are
unchanged.

## Complete Study-Path Audit

The fresh profile-9/type-12 action-5 audit is recorded in
`re/milan/ACTION5-FRESH-AUDIT-20260818.md`. This export's action, positive
packing, single-use probe ownership, and actions `1..5` are covered. First-graph
append, full-capacity retained normalization, action-5 handoff, and
dispatcher-entered action-0 transient finalization are implemented. Shared
evidence gate zero is excluded because this export skips the dispatcher
entirely.

For this profile, the authoritative selector/replacement chain is
`FUN_1800469f0 -> FUN_180045530/FUN_1800458c0 -> FUN_1800452e0`, with
`FUN_180046780`, `FUN_180046ef0`, and `FUN_180071370` owning footprint policy.
Selector/replacement attribution to the alternate-profile `0x180062...` and
`0x180064...` families is not applicable to this type-12 study chain.

The native ordinary wrapper models action 0/index `-1`, validates the probe,
publishes no packed update, and leaves persistence unchanged. Immutable
enrollment-2 sequence 157 is the shared-gate-zero no-dispatch family. Generic
gate-positive no-direct can instead perform capacity shutdown or late enqueue
before the diagnostic-only transient finalizer. Action 5 is exact for handoff,
lifecycle, order, tail, and packed payload.

## Call Completion And Publication

A successful positive `identifyImage` result retains the probe and matched
gallery needed for this export. Calling the export is a study attempt, and its
ordinary status-zero return is a completed study even when the returned update
is zero. This includes the shared-gate-zero path, which skips the dispatcher but
still writes update zero and destroys the retained probe. No-match transactions
do not call this export.

Completion is independent of publication. Update zero exposes no candidate and
does not advance persistence, even when the live queue or lifecycle state changed
transiently. Updates `1..5` leave the same matched gallery handle available for
the adapter's one positive pack; the returned action and that complete packed
object advance together.

Production commits `f6e97782`, `1a858825`, `89dd6c43`, and `a29d5d5f` close the
selector sentinel, exact retained-action predicate, wrapped policy arithmetic,
and replacement scalar lifecycle respectively. Direct witnesses are action/index
`0/unset` for the sentinel tuple, `4/1` for wrapped ratio admission, and `2/1`
for retained flag 2; replacement witnesses are `bd=INT32_MAX -> INT32_MIN` and
old `bc=-1 -> target bc=39`. Closure validation records a clean release build,
all three existing Milan suites (`3/3`), and exact campaign results `450/450`
and `643/643`.

Those campaigns cover Boolean-produced state and do not establish the complete
generic packed-input contract. A 2026-08-18 current-head audit found that native
flag-zero ranked-primary validation counted `b5!=0`, whereas DLL
`FUN_180045530:0x180045620..0x180045645` counts exact `b5==1`. The native codec
and print validator accept non-Boolean packed `b5`, making this a
synthetic-valid action-2 parity defect; known production producers remain
Boolean. The one-predicate correction changes only that count to exact one. The
focused discriminator then returns action `0` with no selected index, matching
the DLL; the existing suites and standing campaigns pass at `450/450` and
`643/643`. The witness is recorded in `FUN_180045530.md` and
`FUN_1800469f0.md`.

The same audit series found an independent synthetic-valid append distinction.
The DLL copies matched `b5` to the appended feature unchanged but installs an
established-graph reference edge only for exact matched `b5==1`; native tested
nonzero. Packed `b5=2` therefore produced one extra native relation. Changing
only the edge predicate to exact one makes the aligned direct append outputs
byte-identical at 39,723 bytes and SHA-256
`a37131112c97939b40d2e228a677c9cc861fa0f9205546e15e84c1d78b215aa4`.
The existing suites and standing campaigns pass at `450/450` and `643/643`.
`FUN_1800469f0.md` records the complete witness.

The native offline API returns a validated candidate for positive actions, and
the production libfprint verify/identify path now runs that runtime and applies
positive template updates. fprintd remains an external consumer of the standard
libfprint driver interface. This integration does not alter the proven official
export behavior.

## Action-5 Handoff Recovered

The remaining action-5 payload ambiguity is resolved. The DLL does perform a
queued matcher lifecycle write, but on authoritative sequence 3 it targets newly
primary-appended feature 9, not feature 8. The primary mutation, queued re-match,
queued ordinary append, queue removal, and action override all operate on one
live gallery before any final order/tail generation. Only after queue exhaustion
does the dispatcher order once; this export then destroys the independent
single-use probe and leaves the same gallery handle for one final pack.

Expected sequence-3 closure is count `9->10->11`, queued score/selected `100/9`,
feature 9 `be 1->2`, queued inner action/new index `1/10`, occupancy `1->0`,
external action 5, 11 features/seven relations, final order
`[10,9,8,7,2,1,0,4,5,6,3]`, and packed SHA-256
`c971239885adccdcaece80b31dc9e5e3abea6b0cc428b1b82307cb05fe12b181`.

This was the pre-fix native state: the callback gallery was current but
prematurely finalized and byte-identical to the primary-only DLL finalized
payload `081c3e...`. Native consequently returned selected 9 with
direct/lifecycle mask `0x300` instead of DLL `0x200`, adding feature-8 `be` and
reordering the prefix. Its Q was a serialized one-feature packed template rather
than the DLL full live deep copy. A gallery-only unfinalized substitution yielded
`39/8`, mask `0x181`, so deferral alone was insufficient. The bounded
implementation task was to retain
full prepared/live Q and one mutable G transaction, preserve matcher
direct/aggregate and selector normalization semantics, defer order/tail and final
publication packing until queue exhaustion, then expose 5 only after a committed
queued selector. Artifact root is
`study-action5-handoff-unit2-v1` under the persistent derived dataset.

Current source implements that ownership and timing contract. It retains the
deep-owned transformed live queue entry, updates one unfinalized shared gallery,
and performs final type-12 order/tail generation once after queue exhaustion.
The current closure and action-2 versus action-3/4 live-overlap distinction are
recorded in `FUN_18005d330.md` and `FUN_180044fc0.md`.

## Full-Capacity Retained Normalization Closure

The remaining pre-policy ordering boundary is recovered for production profile
9/type 12. Inside `FUN_180044fc0`, positive retained count and flag one first
refresh `b5` and relations; exact full capacity then normalizes the complete live
gallery before action admission and selector entry. Actions 3/4 normalize a
second time after replacement. Action 0 retains the pre-policy normalization as
transient live mutation, action 2 has no normalization, and 39-to-40 append has
only post-append normalization. The queue follow-up caller enters the selector
directly and has no prepass.

Ten synthetic-valid controls cover stale/already-normalized `bb`, retained
count/flag zero and one, reference/nonreference entries, actions 0/2/3/4, and the
capacity boundary. The primary stale discriminator selects normalized feature
34 rather than stale feature 13; staged and dispatcher packed bytes are exact at
SHA-256
`5dc15026cc41963fb9342441757608d531ca0c2a488a7664ab6ced7a4faf4ec3`.
No natural row in the earlier 798-row chronology is predicted to change because
all three observed retained-refresh rows are below-capacity appends.

## Unit-5 Export, Ownership, And No-Publication Closure

The word tested at global `0x18024ebd8` before the dispatcher call is recognition
evidence `0x18024e550 + 0x688`. It is not a second gate. Consequently a
successful gate-zero match skips `FUN_180044fc0`, leaves order/tail/queue and
retained state untouched, writes update 0, and still destroys/clears the original
retained probe at `0x18000206f..0x180002076`. This is the natural sequence-157
family and differs from rejected authentication, for which the adapter does not
call this export.

With gate nonzero, dispatcher-entered action 0 can still mutate retained state,
capacity queue state, or a late deep-owned queue copy before accepted order/tail
finalization. The gallery global remains the same live object until adapter
cleanup; this export consumes only the independently owned original probe. A
late queue copy survives that probe cleanup inside the gallery, then is destroyed
when the action-0 adapter path deletes candidate handles.

This export performs no packing for any action. Unit-5 traps prove that its
production callers also invoke neither `templateGetPackedSize` nor `templatePack`
for action 0; a positive action-1 sanity call reaches both. Every action-0
adapter wrapper is zero and no persistent candidate is exposed. Direct controls
and report are under `study-action0-finalization-unit5-v1`, with SHA-256
`2a5798ca8c303b608d5214a97d1fe05592425f985ea9ed51b4bbac68a82dc9d7`
and `85a8f262d0feb69daf56ea52289f1bc8ca9bfc84608340b3db8a99e44c46a45c`.

## Native Unit-5 Ownership Closure

The native study wrappers now expose action-0 state only through a borrowed
diagnostic observer. The observer sees no serializable or reference-counted
candidate and cannot satisfy the positive update contract. The caller retains
and frees the original `GoodixMatchInfo` once; late enqueue owns a prior deep
copy. Every action-0 output remains null, while actions `1..5` retain the
existing positive `GBytes` path.

## Action-4 Continual-Learning Semantics

At the profile-9 limit of 40 learned features, action 4 is the primary
retained-evidence replacement path. It exchanges a comparatively redundant
active nonreference view for a useful accepted probe, installs the matcher-owned
reference relation, recomputes gallery residual coverage, and finalizes one
bounded persistent template. The recognition-selected owner and replacement
target remain separate roles.

Identify epoch 26 demonstrates why byte-exact action-4 validation must begin
before this export. A mixed aggregate-strong/geometrically-weak feature was
incorrectly promoted to direct-positive by a flattened first-veto predicate.
Authentication and action 4 remained positive, but selected owner, lifecycle
mutation, relation evidence, and final candidate diverged. The exact native
predicate and routing remain in `FUN_18005c3b0.md`, `FUN_180053220.md`,
`FUN_180057e60.md`, and `FUN_180055a40.md`. The behavior-level interpretation,
operator capture guidance, and action-4 audit backlog are maintained in
`../IDENTIFY-26-ACTION4-SEMANTIC-AUDIT-20260808.md`.

## Maximum Append Packing Capacity

An append can materialize retained relations before adding the new feature. A
valid 39-to-40 transition can therefore grow the relation matrix by more than
the probe-size estimate that previously bounded the clean-room intermediate
pack buffer. The DLL mutates a preallocated live template and has no equivalent
serialized-size estimate at this boundary.

Production commit `8e4a160` sizes the private clean-room study output to the
existing 1 MiB profile limit, preserving checked packing while accommodating
the maximum valid retained-relation growth. Independent 450- and 643-operation
campaigns pass 1,093/1,093 selected operations after this correction.

## Established-Graph Append `b5` Predicate

After ordinary append, `FUN_1800469f0:0x180046c37..0x180046c4c` calls the
relation installer only for exact matched `b5/+0x110 == 1`. The appended feature
still inherits the matched dword unchanged. Packed `b5=2` is therefore a valid
generic discriminator: append preserves two but does not create its reference
edge. Known natural producers write only zero or one.

Production `c182b140` uses the exact-one edge predicate. A direct two-feature
`b5=2` control preserves the appended dword without creating a reference edge;
its output is byte-identical to the DLL at 39,723 bytes and SHA-256
`a37131112c97939b40d2e228a677c9cc861fa0f9205546e15e84c1d78b215aa4`.
