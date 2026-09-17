# Profile-9/Type-12 Match Contribution Contract

## Scope And Owners

This document maps the profile-9/type-12 matcher orchestration in
`GoodixEngineAdapter.dll` 2.0.310.900: caller state, traversal, contribution,
independent selection owners, blocking, finalization, and live mutation.
The body is [FUN_180055a40](functions/FUN_180055a40.md) at
`0x180055a40..0x180057135`. Its function note owns candidate construction,
policy joins, complete field layout, and detailed producer constraints.
Correspondence, affine fitting, bitmap scoring, rescue internals, and study
selection belong to their linked function notes.

| Entry | Native owner and role |
|---|---|
| Ordinary match | [FUN_18005edb0](functions/FUN_18005edb0.md) calls the type-12 body at `0x18005eec1` with the original live probe and gallery |
| Queued rematch | [FUN_18005d330](functions/FUN_18005d330.md) calls the same body against the already-mutated gallery, restricted to one physical trigger |
| Combined match/study | [FUN_18005ef60](functions/FUN_18005ef60.md) contains the type-12 composition; its references are unwind metadata, with no direct code caller |

Related current owners are `milan_match_prepared_probe()` in
`drivers/goodix53x5/milan/match/match.c`, `GoodixMilanMatchSelection` and
`goodix_milan_match_selection_block_candidate()` in `match/selection.c`, and
the serialized mutation boundary in `match/lifecycle.c`.

## Invocation And Caller State

Matcher entry saves incoming evidence `+0x690` as the blocker-enable seed
**before** clearing all `0x698` evidence bytes at `0x180055b34..0x180055b3d`.
Selected physical feature `+0x648` starts at `-1`; selected count `+0x64c` and
active-relation count `+0x668` start at zero; selected/routed affines at
`+0x650/+0x66c` start at identity; score starts at zero. Candidate rows, Q8
sum/count, blocker count/sum, fallback workspaces, late-status count and
correspondence matrices are invocation-local. The incoming seed is an enable,
not an initial blocker count or sum.

[FUN_18005e230](functions/FUN_18005e230.md) initializes configuration through
word 17. The callers supply the remaining state:

| State | Ordinary `05edb0` / `identifyImage` | Queued `05d330` |
|---|---|---|
| Word 13 | One: ordinary traversal | Zero: trigger-only traversal |
| Word 14 | Unwritten and not consumed while word 13 is one | Current continuation-ring physical index |
| Word 18 | Live probe `c7/+0x150`, fixed for this invocation | Forced zero, regardless of stored queued `c7` |
| Word 19 | Recognition one on each gallery dispatch; policy may clear it | Initially unwritten stack content; one reused policy object carries matcher mutations across occupied entries |
| Incoming evidence `+0x690` | `identifyImage` rewrites its operation-level anti-fake argument before each dispatch | Initially unwritten stack content for the first occupied rematch; later rematches receive zero because the matcher clears the reused evidence and no intervening owner rewrites this field |

A null optional-result pointer does not disable blocking. Queued matching is
not intrinsically blocker-free: a nonzero first seed can admit the same blocker
ladder. The two unwritten queued words have separate storage and lifetimes;
evidence clearing does not initialize policy word 19. Their exact stack overlap
and downstream consumers are in `FUN_18005d330.md`.

The ordinary live probe is read-only across gallery comparisons. Matching does
not replace its records, partition, masks, bitmaps, histogram, anti-fake object,
or packed class. Queue insertion deep-copies into independent owners. Native
matching performs no gallery normalization on entry: a retained handle supplies
the preceding study action's live state. See
[FUN_180044fc0](functions/FUN_180044fc0.md) for normalization boundaries.

## Index Domains And Traversal

| Domain | Owner | Uses |
|---|---|---|
| Physical feature index | Tag-`95` position / gallery feature array `+0x28` | Candidate row, contributor slot, retained entry, selected feature, lifecycle word, and graph endpoint |
| Traversal occurrence | Tag-`a1` order / gallery `+0x87e8` | Candidate chronology, policy mutation, Q8 prefixes, tie order, and immediate direct writes |
| Graph reference index | Tag `f2` / gallery `+0x87d8` | Destination of active-producer affine routing |

The matcher loads `a1[occurrence]` at `0x180055d50` and clears that physical
`0x134`-byte candidate row before testing the loop-top gates. Valid order is a
permutation of physical indices `0..feature_count-1`; the matcher does not
validate it. Inactive retained entries append in traversal order but carry
physical indices and direct affines. Rescue receives the original order plus
physical-indexed rows. Successful no-direct lifecycle increments instead visit
contributors in ascending physical order, then sort gallery order.

The exact loop-top skip is:

```text
(configuration[13] == 0 && physical_index != configuration[14])
|| ((feature_count == maximum_features || rejection_evidence == 1)
    && retained_active_evidence == 1 && feature.b5 == 1)
```

This precedes enrolled-feature `c7` decoding, accumulated-high mutation, and
the one-shot latch. For a non-skipped occurrence, the late state saves
`min(5, prior_high + feature_high)` before updating accumulated high to
`max(prior_high, feature_high)` when `feature_high > 3`. The late-policy tuple
is `[updated_accumulated_high, independent_histogram_class, saved_capped_sum]`.
Packed probe classes and histogram class are independent inputs.

The one-shot branch at `0x180055e12..0x180055e34` increments accumulated high
and clears the local continuation gate when the prior status-one count exceeds
five, accumulated high is below five, and the latch is clear. Otherwise a prior
count above ten exits traversal before candidate construction. A late-status
return skips contribution and publication but preserves its completed physical
row; a contextual veto can preserve a partial row before affine/low-bitmap
materialization. `FUN_180055a40.md` owns these row and cutoff contracts.

## Contribution And Independent Selection

After candidate construction and late policy, contribution requires:

```text
flag64 == 1
|| (candidate[4] > configuration[0] + 207 && candidate[5] > 195)
```

Each contribution marks its physical slot, increments contributor count, adds
`(candidate[1]*256 + 21)/42` to the Q8 prefix, and evaluates ranked-candidate
replacement **before** blocking. Dword operations wrap and signed division
truncates toward zero. Ordinary retained counts `5..42` and at most 40 features
keep this prefix arithmetic in range. Later blocking never rolls it back.

| Owner | Replacement/publication rule | Consumer |
|---|---|---|
| Q8 contributor prefix | Every contributor in traversal order | Direct relatch and successful no-direct final score |
| Ranked 77-word candidate | Signed lexicographic key `[5,1,9]` (detail, retained count, coverage), with entering acceptance/rejection and flag-dependent replacement arms | Compact validator and optional result coverage |
| Inactive retained entries | `(candidate[0] > 6 && candidate[5] > 208) || flag64 != 0`, with `b5 == 0` | Retained physical indices and their direct affines |
| Retained-active evidence `+0x644` | Latches on a nonblocked direct occurrence with `flag64 != 0` and `b5 != 0` | Later loop-top active-feature skip |
| Direct selection | Nonblocked `flag64 != 0`, strictly greater candidate word 1 | Selected physical feature, selected count, direct affine |
| Active relation producer | Nonblocked `flag64 != 0`, `b5 == 1`, strictly greater word 1 | Independent relation count and graph-reference-routed affine |
| Rescue selection | [FUN_18005d9e0](functions/FUN_18005d9e0.md) publication | Rescue score/index and conditional affine replacement |
| Fallback selection | Separately enabled physical workspaces | Positive score without selected-feature or lifecycle publication |

The ranked vector is not the direct or active-relation winner. Its strict key
comparison preserves ties, but unconditional flag arms can replace an equal or
lower rank; the complete entering-evidence table is in `FUN_180055a40.md`.

Inactive retention and retained-active evidence have different writers.
`0x180056dfc..0x180056e61` appends only inactive features satisfying the retention
predicate. `0x180056eaa..0x180056ed2` separately requires nonzero `flag64` before
latching the active flag. An active metric-only retained candidate cannot set
that flag. Relation routing and loop-top skipping additionally require exact
`b5 == 1`, rather than merely nonzero.

Every nonblocked direct occurrence increments live `be/+0x134` immediately at
`0x180056f72` and relatches score to `(q8_sum*100/contributor_count) >> 8`.
A lower/equal retained count can therefore relatch score without replacing the
selected feature or affine. Nonzero flag64 and flag60 are separately OR-latched
as Boolean acceptance `+0x684` and rejection `+0x688`. A zero continuation gate
ends traversal after the direct publication; a nonzero gate permits continuation.
Fallback staging requires acceptance still zero, configuration word 13 exactly
one, and retained count at least three; its owner is
[FUN_180079060](functions/FUN_180079060.md).

## Anti-Fake Blocking

Only contributors reach blocking. It requires the saved caller seed nonzero,
both anti-fake owners present, and both feature coverages strictly above 40.
[FUN_18003a3e0](functions/FUN_18003a3e0.md) compares the enrolled and probe
anti-fake objects under the feature-local affine; it does not mutate either.
Its five-word result is caller-zeroed, and words 1..4 supply pair count,
symmetric support, directional support, and raw lower-median detail.

The raw-detail domain is `0..192`, including no-pair raw detail zero. The
persisted pair score `+0x1ab4` is separately `-1` or a producer-derived value in
`0..192`; `-1` is not a raw pair-detail output. Enrollment's pair-score averaging
owner is [FUN_180041ac0](functions/FUN_180041ac0.md).

Scalar deltas are wrapped gallery-minus-probe dwords at `+0x12c4`, `+0x12c8`,
and `+0x1ab0`, then compared as signed values. The ladder reads persisted pair
score `+0x1ab4`, not boundary-classification scalar `+0x12d0` or model score
`+0x12d8`. Its complete instruction span is `0x180056794..0x180056c1c`.
The detail operands below are conjuncts within that ladder, not independent
sufficient predicates:

| Ladder term | Raw-detail conjunct |
|---:|---|
| 7 | `raw_detail <= 77` |
| 8, 9 | `raw_detail <= 78` |
| 10 | `raw_detail <= 75` |
| 16, 17 | `raw_detail >= 70` |
| 18, second alternative | `raw_detail >= 69` |
| 24 | `raw_detail <= 76` |

Term 18's first alternative and the other terms do not inspect raw detail.
The coverage-bearing term with texture/shape/boundary minima `15/295/320`,
persisted score at least 93 and directional support at least 3200 admits
`candidate[9] >= 95`, including equality. `FUN_180055a40.md` records the full
twenty-pair/77, eight-pair/76, and coverage-95 conjunctions and branch addresses.

Adjustment is used only for blocker accumulation, never predicate admission:

```text
adjustment = -20
if persisted_pair_score != -1:
    if persisted_pair_score < 70:
        adjustment = -10
    if persisted_pair_score < 60
       || (boundary_delta > 100 && persisted_pair_score < 75):
        adjustment = 0
adjusted_detail = wrap32(raw_detail + adjustment)
```

Thus `-1` always selects `-20`; scores `0..59` select zero; `60..69` select
`-10` unless delta exceeds 100; `70..74` select `-20` unless delta exceeds 100;
`75..192` select `-20`. Adjusted evidence lies in `[-20,192]`. Only a blocking
hit increments the invocation's initially zero blocker count and adds this
adjusted value to its wrapping sum at `0x180056c22..0x180056c26`.
At most 40 contributing features cannot overflow either accumulator.

A hit skips inactive retention, acceptance/rejection latching, direct selection,
active relation, lifecycle writes and fallback staging for that occurrence.
It retains the physical candidate row, contributor mark, Q8 term/count and any
ranked-vector replacement. Raw/adjusted anti-fake detail does not replace a word
in the 77-word candidate; after this occurrence, only blocker count/sum consumes
the adjusted value.

## Ordered Post-Loop Finalization

1. Run rescue when contributor count exceeds one, rejection is zero, and blocker
   count is zero. Rescue reads physical rows, stored order and current score.
   It may replace score/selection but adds no lifecycle increment and preserves
   the earlier active-relation owner. Partial/contextually rejected and completed
   late-status rows remain available to rescue.
2. If the local continuation gate is zero, clear rejection `+0x688` at
   `0x180056cba`. This clears all rejection, including a strong rescue write.
   The earlier rescue-admission test sees rejection before this clear.
3. If optional result storage exists, copy the evidence-selected affine to
   `+0x08..+0x1c` and ranked candidate word 9 to `+0x20`. These are independent
   owners. Ordinary `identifyImage` supplies null storage.
4. Evaluate queue insertion from the effective rejection after rescue/clear.
5. Compute the terminal ratio predicate:

   ```text
   (blocker_count > 5 && blocker_sum > blocker_count*60)
   || (blocker_count > 10 && blocker_sum > blocker_count*50)
   ```

6. A nonzero existing score bypasses compact/fallback finalization. Replace only
   that score with `-65536` if the ratio predicate holds. The same ratio route
   handles zero score when accumulated high is at least four or histogram class
   is at least two. Otherwise, configuration word 13 zero returns zero without
   compact/fallback evaluation.
7. On the ordinary zero-score path, compact validation requires a contributor,
   configuration word 18 exactly zero, blocker count zero and nonzero
   [FUN_1800608b0](functions/FUN_1800608b0.md) result on the ranked vector.
   Its return is a gate, not the published score. Success increments each
   physical contributor in ascending order, calls `FUN_1800607b0` to sort order,
   and publishes the final Q8 prefix score. It does not set acceptance or publish
   a selected index/affine.
8. Otherwise run [FUN_18005d5f0](functions/FUN_18005d5f0.md) on enabled fallback
   workspaces. Positive fallback sets acceptance and returns, even with blockers.
   Only nonpositive fallback is replaced with `-65536` for any positive blocker
   count. Fallback adds no lifecycle owner or selected index/affine; its optional
   coverage can replace the earlier ranked coverage. It does not revisit queue
   admission or rejection evidence.

The continuation gate is distinct from evidence `+0x68c`. That evidence starts
at one and is replaced by `FUN_18005e480(probe,gallery)` only for nonzero live
probe `c0/+0x14c`. The local gate starts as
`configuration[4] != 0 && evidence[+0x68c] != 0`, then clears for
`FUN_180073060(configuration[18])` high mode nine or the traversal one-shot
branch. Ordinary extraction leaves `c0` zero. Packed `c7=0x500` decodes to
high mode nine for this gate. The same local controls direct-positive
continuation and post-rescue rejection retention; it is not itself a queue test.

## Queue And Lifecycle Publication

At `0x180056cee..0x180056d4b`, insertion through
[FUN_1800462a0](functions/FUN_1800462a0.md) requires exactly:

```text
rejection[+0x688] == 0 && configuration[13] == 1
&& decoded_probe_low == 0 && probe.coverage > 65 && probe.quality > 15
&& accumulated_high < 4 && probe_histogram_class < 3 && gallery.queue_state == 0
```

There is no positive-score or selected-feature requirement. Aggregate,
fallback, or blocker finalization cannot undo an earlier insertion. State-zero
unpack owns 20 empty queue owners with ranks `-1`; feature capacity and queue
state are separate fields. Queue bodies/ranks are transient, not part of packed
gallery bytes. Only queue state and transaction count are encoded; see
[OUTER-TEMPLATE-CODEC](OUTER-TEMPLATE-CODEC.md).

For ordinarily extracted probes, insertion requires stored packed `c7` in
`{0x000,0x100,0x200}`; the copied queued field is preserved even though rematch
configuration word 18 is zero. Insertion needs rejection zero, while
`templateStudy` enters its dispatcher only with rejection nonzero. A newly
inserted entry cannot be consumed by the same match's study. The ordinary adapter
destroys transient queue state after action zero; retained-handle continuation
and action-five consumption belong to `FUN_18005d330.md` and
[templateStudy](functions/templateStudy.md).

| Route | Score / selected owner | Relation owner | Lifecycle increments |
|---|---|---|---|
| Direct | Current Q8 prefix / strict retained-count selection | Strict active producer, or initialized zero/identity | Every unblocked direct occurrence |
| Direct then rescue | Rescue publication; affine can remain the earlier direct affine | Earlier active producer | Earlier direct occurrences only |
| Rescue only | Rescue publication | Initialized zero/identity | None |
| No-direct aggregate | Final Q8 prefix; selected may stay `-1` | Initialized zero/identity | Every physical contributor |
| Positive fallback | Fallback score; selected stays `-1` | Initialized zero/identity | None |
| Direct then ratio override | `-65536`; earlier selected evidence preserved | Earlier active producer | Earlier direct occurrences remain earned |
| Rejection or terminal zero without direct publication | No selected owner | Initialized zero/identity | None |

Blocking and direct publication exclude each other only within one occurrence.
Distinct features can contribute blockers and an unblocked direct owner in one
call. Each gallery feature owns its records, anti-fake object, active state,
coverage, packed class and candidate transform. The shared probe, mutable policy,
Q8 prefix and evidence introduce ordering; they do not equate those feature-local
results. The smallest first-ratio composition uses six blockers with sum greater
than 360 plus one direct feature; the second uses eleven with sum greater than
550 plus one direct feature. Blockers may precede or follow the direct owner
subject to traversal gates. Nonzero blocker count excludes rescue and compact
success but does not erase an already latched direct score, selected evidence,
relation or `be` writes. A terminal `-65536` alone does not identify the ratio
route: nonpositive fallback with any blocker has that same score.

Native has no direct/lifecycle bitmask. Direct increments occur during traversal;
aggregate increments occur only after compact success. Both wrap live
`be/+0x134` modulo `2^32`. Selected feature is not the set of lifecycle owners,
and negative final score does not roll back earlier increments. Current physical
direct/contributor/lifecycle masks represent these native events; they must not
be interpreted as graph indices or traversal positions.

For a valid nonempty type-12 probe, terminal matching returns status zero; the
caller separately accepts signed `score > 0`. Empty live records return
`0x80000006`. `identifyImage` does not pack the gallery: subsequent
`templatePack` at `0x1800020b0` serializes its live mutations even after a
nonpositive score. Matcher-owned lifecycle updates change `be`; normalization
has separate `bb`/`a1` ownership. No matcher failure or later study rejection
provides transactional rollback of earlier live writes.

## Detailed Cross-References

- [FUN_180055a40](functions/FUN_180055a40.md): full evidence layout, candidate
  rows, policy state, rank replacement arms, blockers, and finalization.
- [FUN_18005e3e0](functions/FUN_18005e3e0.md): standalone evidence initializer;
  the matcher's incoming-seed-before-clear contract is separate.
- [FUN_180053220](functions/FUN_180053220.md),
  [FUN_180058700](functions/FUN_180058700.md), and
  [FUN_1800553f0](functions/FUN_1800553f0.md): mutable flags/recognition, late
  status, and final clear-only policy.
- [FUN_1800619a0](functions/FUN_1800619a0.md): physical producer to graph-reference
  routing, including its transform and relation-slot ownership.
- [FUN_18005d9e0](functions/FUN_18005d9e0.md),
  [FUN_1800608b0](functions/FUN_1800608b0.md), and
  [FUN_18005d5f0](functions/FUN_18005d5f0.md): independent rescue, compact, and
  fallback contracts.
- [FUN_18003a3e0](functions/FUN_18003a3e0.md): five-word anti-fake result and
  tight raw-detail bound; [FUN_180041ac0](functions/FUN_180041ac0.md): persisted
  endpoint pair-score update after admitted enrollment relations.
- [FUN_18005d330](functions/FUN_18005d330.md),
  [FUN_1800462a0](functions/FUN_1800462a0.md), and
  [FUN_180044fc0](functions/FUN_180044fc0.md): queued policy lifetime, insertion,
  study consumption and finalization.
