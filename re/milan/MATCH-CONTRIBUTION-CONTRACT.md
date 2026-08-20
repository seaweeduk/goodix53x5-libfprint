# Profile-9/Type-12 Match Contribution Contract

## Scope And Authority

This document owns the orchestration contract around the profile-9/type-12
matcher. It covers dispatch, traversal order, match-local state, contribution,
retained/direct publication, relation ownership, anti-fake blocking, and
post-loop finalization. It deliberately excludes correspondence, affine fitting,
overlap, score-policy thresholds, rescue internals, study action selection,
malformed templates, and every other sensor type.

The Windows authority is `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
The relevant body is `FUN_180055a40` at
`0x180055a40..0x180057135`. `FUN_18005edb0` dispatches ordinary subtype-12
matching to this body at `0x18005eec1`. `FUN_18005d330` uses the same body for
queued rematching. `FUN_18005ef60` also contains a subtype-12 match-and-study
composition, but has no resolved code caller.

The clean-room comparison is production commit
`c182b140a86e70312bd9f637ba68cf8a9140f6c0`, principally
`milan_match_prepared_probe()` in
`drivers/goodix53x5/milan/match/match.c` and
`GoodixMilanMatchSelection` in `match/selection.c`.

## Invocation State

Every matcher call owns fresh evidence, candidate, fallback, rescue, late-status,
and matrix workspaces. The DLL clears the `0x698` evidence object at
`0x180055b34`, initializes selected feature `+0x648` to `-1`, initializes
selected and routed affines at `+0x650/+0x66c` to identity, and initializes score
to zero. Current source performs equivalent output and selection resets before
template traversal.

`FUN_18005e230` initializes the persistent 20-word policy for ordinary calls.
It writes configuration word 13 (`+0x34`) to one and does not write word 14
(`+0x38`); word 14 is irrelevant while word 13 is one. The initializer stops at
word 17. Ordinary dispatcher `FUN_18005edb0` then copies live probe `c7/+0x150`
to word 18 at `0x18005ee1b` and writes recognition word 19 to one at
`0x18005ee10`.

Queued caller `FUN_18005d330` initializes one policy object before its queue
scan, changes word 13 to zero, and writes each continuation-ring physical index
to word 14. It forces word 18 to zero at `0x18005d3ec` and has no writer for word
19. The first queued call therefore receives the preexisting stack-slot value of
word 19; any policy clear then carries through the reused object to later
occupied queue entries. Evidence, matrices, late context, and status counters
remain per-call. The configured-index gate skips every gallery feature except
word 14's physical slot, but policy reuse introduces no queue-specific
contribution threshold, score formula, relation rule, or lifecycle rule.

For an ordinary call, the probe's packed `c7` remains fixed in configuration word
18 for the invocation and word 19 starts at one. Policy may clear word 19 while
processing one feature, and later traversal occurrences observe the cleared
value. For a queued DLL scan, word 18 remains forced zero and word 19 retains its
preexisting/carry value. Current source instead initializes a fresh
`GoodixMilanMatcherPolicy` in every queued
`goodix_milan_match_info_result()` callback, copies that queued probe's `c7` to
word 18, and starts word 19 at one. The per-call late-policy status counter and
accumulated high class persist across traversal occurrences but always reset
before the next queued matcher call.

## Three Index Domains

Valid profile-9/type-12 matching has three distinct index domains:

| Domain | Owner | Use |
|---|---|---|
| Physical feature index | Serialized tag-`95` position | Feature object, graph endpoint, contributor/direct slot, rescue row, selected feature, and lifecycle bit |
| Traversal occurrence | Tag-`a1` position | Candidate chronology, mutable policy state, Q8 prefix order, strict-tie order, and immediate direct lifecycle chronology |
| Graph reference index | Graph tag `f2` | Route the strict active producer's direct affine to the graph reference |

The DLL loads `a1[occurrence]` at `0x180055d50`, then uses that value to address
the physical feature pointer at template `+0x28`. Current source reads the same
dword from `enrolled->tail_state` and keeps `order_index` separate from
`feature_index`. Candidate diagnostics may record both, but all durable slots and
bit masks use `feature_index`.

The valid template contract supplies a unique permutation of physical indices
`0..feature_count-1`. Direct writes therefore occur in `a1` order, while the
successful no-direct aggregate loop at `0x180057050..0x18005706d` visits
contributor slots in ascending physical order. Reordering `a1` can change Q8
prefixes, mutable policy state, tie winners, and immediate direct-write order; it
must not rebase contributor bits, selected indices, graph references, or
aggregate writes.

Retained inactive evidence is chronology-ordered because entries append as the
loop encounters them, but each entry carries a physical feature index and that
feature's direct affine. Rescue receives the original `a1` permutation alongside
its physical-indexed metric rows.

## Packed Mode And Loop-Top Order

The probe packed mode is decoded once before traversal. Each reached enrolled
feature has its own packed `c7` decoded for the three-word late-policy state. A
feature high class above three raises the match-level accumulated high class for
later traversal occurrences; lower classes affect only the current state.

The DLL's loop-top configured/full-gallery skip at
`0x180055d80..0x180055db9` precedes enrolled-feature `c7` decoding at
`0x180055dcd`. Current source calls
`goodix_milan_matcher_late_context_derive()` immediately before
`goodix_milan_match_candidate_skip_pre_primary()`. This is a static ordering
difference, but it has no effect for the authoritative valid population. All
40,719 enrolled-feature instances in the 1,104 strict gallery inputs have packed
feature `c7 == 0`, so an early derive cannot mutate accumulated state. Fresh
probe-mode-0/1/2 output parity below supplies no source correction. A natural
valid enrolled feature with a decoded high class above three would be required
before assigning production reachability to this difference.

For ordinary calls the exact loop-top skip is:

```text
skip when
  (feature_count == maximum_features or rejection_evidence == 1)
  and retained_active_evidence == 1
  and physical_feature.b5 == 1
```

Queued calls first apply their independent configured-index gate. All equality
tests above are exact; a general nonzero value is not interchangeable with one.

## Per-Occurrence State Machine

After a physical feature survives the loop-top gate, orchestration is:

1. Build or refresh the feature's candidate and fallback workspace through the
   separately documented leaf helpers.
2. Run mutable policy. A nonzero late-status return continues directly to the
   next traversal occurrence before contribution, retained evidence, relation,
   direct publication, lifecycle state, or fallback enablement.
3. Contribute when final `flag64 == 1`, or when metric 4 is strictly greater
   than `configuration[0] + 207` and metric 5 is strictly greater than 195.
4. Add the wrapped Q8 term to the prefix, increment contributor count, mark the
   physical contributor slot, and evaluate the independent winner rank. These
   effects occur before anti-fake blocking and are not rolled back by a later
   block or skip.
5. In the ordinary caller only, evaluate anti-fake blocking when both anti-fake
   objects and their coverage gates are present. A blocking hit increments the
   match-level blocker count/sum and skips retained/direct handling. Queued and
   combined local-evidence calls have a zero blocker seed.
6. For every nonblocked candidate, normalize nonzero `flag60` into Boolean
   rejection evidence. Retain when `(metric0 > 6 && metric5 > 208)` or final
   `flag64 != 0`. A retained inactive feature appends physical index plus affine;
   a retained active feature sets the retained-active flag instead.
7. Normalize nonzero `flag64` into Boolean acceptance evidence. Every nonzero
   `flag64` marks the physical direct slot, earns its lifecycle occurrence, and
   relatches score from the current Q8 prefix. Strictly greater metric 1 alone
   replaces selected physical feature and selected direct affine; a tie keeps
   the earlier traversal producer while still relatching and earning lifecycle.
8. If the direct occurrence's live `b5` equals exactly one, strictly greater
   metric 1 may independently replace the active-relation producer and route its
   direct affine to graph `f2`. Equal or lower counts preserve the earlier
   producer even if selected score state changes.
9. If acceptance is still clear and a fallback workspace has sufficient retained
   pairs, enable that physical workspace for post-loop fallback evaluation.

The winner used by no-direct finalization is ranked separately by metrics
`[4,1,8]` under the current Boolean acceptance/rejection evidence. It is neither
the strict metric-1 selected feature nor the strict active-relation producer.

## Independent Match-Local Owners

The following workspaces may intentionally identify different physical
features:

| Workspace | Replacement rule | Final consumer |
|---|---|---|
| Q8 contributor prefix | Every contribution in traversal order | Direct relatch and no-direct aggregate score |
| Retained winner | Evidence-gated strict rank on metrics `[4,1,8]` | No-direct validation |
| Direct selection | Strict metric 1 | Normal selected index and affine |
| Active relation producer | Active `b5 == 1`, strict metric 1, routable graph edge | Study relation evidence |
| Rescue selection | `FUN_18005d9e0` result | Rescue score, selected index, and selected affine |
| Fallback selection | Enabled fallback workspaces | Positive score without a selected feature |

No finalization path may collapse these into one generic winner. In particular,
rescue may replace score/selected affine while the earlier active producer keeps
relation evidence. A lower-count later direct result may relatch score while the
earlier direct selection remains. A no-direct winner may publish a positive
score while selected feature remains `-1`.

## Post-Loop Finalization

The DLL post-loop order is:

1. Preserve physical contributor/direct slots and strict active-relation
   evidence produced during traversal.
2. Optionally run aggregate rescue at `0x180056c75..0x180056caf`. Rescue sees the
   traversal score, contributor rows, physical `a1` order, and current rejection
   evidence. A successful rescue writes score/selection evidence before any
   queue decision.
3. At `0x180056cb4..0x180056cc1`, clear rejection evidence `+0x688` when the
   stack-local continuation gate `[rbp-0x50]` is zero. Nonzero rescue rejection
   survives only when that local remains nonzero.
4. Publish the strict active-relation count and routed affine independently of
   whichever score/selection route survives.
5. Evaluate the match-time queue predicate at `0x180056cee..0x180056d4b` from
   post-rescue rejection evidence, configuration, probe classes, quality,
   coverage, and queue state. This is after rescue but before normal aggregate
   publication and fallback.
6. Compute the terminal blocker-ratio Boolean at
   `0x180056d54..0x180056d75`.
7. If traversal or rescue already published a nonzero score, preserve it unless
   the blocker Boolean is set, in which case `0x180057109` replaces it with
   `-65536`; then return without aggregate or fallback evaluation.
8. Apply class/configuration zero-score gates when no direct or rescue score was
   published.
9. Validate the retained winner for no-direct aggregate publication. This route
   requires a contributor, packed mode zero, zero blocker count, and a positive
   winner validator result.
10. Evaluate enabled fallback workspaces only if normal finalization did not
   publish. Positive type-12 fallback publishes score and acceptance evidence but
   leaves selected feature `-1` and adds no lifecycle owner.
11. Otherwise publish the terminal rejection/zero result. A nonzero blocker
    count makes rescue caller-ineligible. The blocker override does not rewrite
    selected feature, transform, relation, evidence, or lifecycle events.

The stack local `[rbp-0x50]` is the matcher continuation and rescue-rejection
retention gate. Its other consumer at `0x180056f88` decides whether traversal
continues after a direct-positive publication. At
`0x180055b7d..0x180055b9c`, evidence `+0x68c` starts at one and is replaced by
`FUN_18005e480(probe, enrolled)` only when live probe `c0/+0x14c` is nonzero.
The gate is initialized at `0x180055bba..0x180055c1b` as:

```text
configuration[4] != 0
and (probe.c0 == 0 ? 1 : FUN_18005e480(probe, enrolled)) != 0
and decoded configuration[18] high mode != 9
```

Configuration word 18 is ordinary probe `c7/+0x150`. The late one-shot branch
at `0x180055e12..0x180055e34` also clears the gate when the match-level
`FUN_180058700` status counter is greater than 5, accumulated decoded high class
is below 5, and the one-shot flag is still zero; that branch increments the high
class and latches the one-shot flag at the same time. Current-source inputs are
not general enough to reproduce the conditional `FUN_18005e480` term:
`milan_match_probe_result_internal()` zero-initializes `GoodixMilanFeatureView`
and transports partition plus `optional_c7`, but not `tagged_values[10]`.
Ordinary extraction therefore reaches this matcher boundary with `c0=0`. The
working-tree production correction intentionally implements that
driver-reachable specialization as configuration word 4 nonzero and raw
`optional_c7` high class not equal to 5, plus the recovered late one-shot clear.
It does not claim to implement the DLL's general nonzero-`c0`
`FUN_18005e480` behavior.

Evidence `+0x688` is rejection evidence. It is zeroed with the evidence block at
`0x180055b34`, accumulates per-feature rejection at
`0x180056e8e..0x180056ea3`, is written to one by strong rescue at
`FUN_18005d9e0:0x18005dd4e`, may be cleared at `0x180056cba`, and is the first
queue comparison at `0x180056cee`. The retention gate is not itself a queue
predicate; it decides whether rescue rejection reaches that comparison.

Final ownership by route is:

| Route | Score/selection owner | Relation owner | Lifecycle owner |
|---|---|---|---|
| Direct | Q8 prefix plus strict direct selection | Strict active producer, or initialized zero/identity | Every physical direct slot |
| Direct then rescue | Rescue score/selection | Earlier strict active producer, or initialized zero/identity | Earlier physical direct slots |
| Rescue only | Rescue score/selection | Initialized zero/identity | None |
| No-direct aggregate | Final Q8 prefix; selected may remain `-1` | Initialized zero/identity | Every physical contributor slot |
| Positive fallback | Fallback score; selected remains `-1` | Initialized zero/identity | None |
| Direct then blocker override | Score `-65536`; earlier direct selection remains | Earlier strict active producer, or initialized zero/identity | Earlier physical direct slots |
| Rejection or terminal zero | No selected feature | Initialized zero/identity | None |

The DLL constructs no direct or lifecycle bit mask. A nonzero final direct flag
branches at `0x180056eaa`, increments that physical feature's `be/+0x134`
immediately at `0x180056f72`, and publishes the running direct score at
`0x180056f78..0x180056f84`. A blocking occurrence instead increments the
match-local blocker count and sum at `0x180056c22..0x180056c26` and skips direct
handling for that occurrence. Successful no-direct aggregate finalization
increments physical contributor slots at `0x180057058`. Current source
represents the direct and aggregate occurrences with one physical lifecycle mask
and applies the increments at its serialized wrapper boundary.
Rescue preserves already earned direct bits but adds no selected bit; aggregate
finalization replaces an empty mask with contributor bits; fallback and ordinary
rejection add none. A blocker-overridden direct result retains its earned direct
events even though its terminal score is negative.

## Result And Mutation Boundary

`FUN_180055a40` returns zero for every valid terminal match computation. Score
sign belongs to its caller: signed `score > 0` accepts a candidate, while zero or
negative score rejects it. Malformed input or helper failure uses the separate
matcher-error path and does not participate as a score.

The clean-room `GoodixMilanMatchResult` fields have these owners:

| Field | Publication contract |
|---|---|
| `score` | Direct Q8 relatch, rescue score, no-direct final Q8 score, positive fallback score, terminal rejection reason, or blocker override `-65536` |
| matched feature / transform | Strict direct selection, or rescue selection; no-direct aggregate and type-12 fallback keep the sentinel and initialized identity |
| relation | Strict active-relation producer, independent of final score/selection; validity is derived from a non-sentinel selected feature and nonnegative relation count |
| direct mask | Every physical slot whose nonzero final candidate flag earned the immediate native write |
| contributor mask | Every physical slot admitted into the Q8 prefix |
| lifecycle mask | Direct mask once any direct event occurred; otherwise contributor mask only after successful no-direct aggregate finalization |
| retained evidence | Chronology-ordered inactive physical slots plus affines, and the independent retained-active flag |
| finalization gate | Acceptance evidence from direct, rescue, aggregate, or positive fallback publication |
| action gate | The DLL publishes final `+0x688` after the conditional `0x180056cba` clear; clean-room commit `c182b140` combines traversal and strong-rescue rejection without that local clear |
| queue eligibility | The queue predicate; the DLL evaluates rejection evidence after rescue and the conditional `0x180056cba` clear, while clean-room commit `c182b140` freezes it before rescue |

The native mutation order is intentionally split. Direct `be` increments and
direct score publication occur inside traversal. Rescue then runs, the queue may
deep-enqueue, the blocker override is resolved, successful no-direct aggregate
finalization increments contributor `be` dwords in ascending physical order, and
fallback runs last. `FUN_180055a40` returns status zero at `0x180057111` for a
normal terminal computation; `FUN_18005edb0` propagates that status and
`identifyImage` separately tests the signed score at `0x180001e87`.

`identifyImage` does not pack the candidate. The live object remains mutated on
both positive and nonpositive score returns. A subsequent `templatePack` export
at `0x1800020b0` serializes those `be` changes. The natural parity oracle invokes
that export immediately after `identifyImage`, before any possible study call,
to define its after-match bytes. Queue data is not part of that packed object.

The clean-room wrapper first normalizes the enrolled bytes and computes the
matcher result without serialized mutation. When final score is positive and
the lifecycle mask is nonzero, it applies that mask through one
unpack/increment/repack boundary. It publishes `updated_feature` and only then
deep-enqueues. The updater visits physical slots in ascending order and wraps
each `uint32_t` count modulo `2^32`. Normalization owns any `bb` and `a1` changes;
the lifecycle mask owns only `be` changes. Relation, graph, and other metadata
are repacked without matcher-owned changes.

## Clean-Room Boundary Predicates

The two queue/lifecycle conjunctions below distinguish or can distinguish commit
`c182b140` from the DLL. The first now has a focused controlled-valid witness
derived from one natural transaction. The strict unmodified population contains
no occurrence of either complete conjunction; the second remains unwitnessed.

The first is post-rescue queue suppression:

```text
rescue succeeds and sets rejection evidence
and pre-rescue rejection evidence was zero
and native stack-local continuation gate [rbp-0x50] remains nonzero
and configuration[13] == 1
and probe low class == 0
and coverage > 65 and quality > 15
and accumulated high class < 4
and probe primary histogram class < 3
and queue state == 0
```

The DLL rescue runs first. A zero continuation local clears `+0x688` at
`0x180056cba`; otherwise strong-rescue rejection survives and suppresses
`FUN_1800462a0`. Base current source computes `queue_candidate_eligible` before
rescue and later enqueues from that value. Separately, `match.c:1907..1910`
combines traversal and rescue rejection without the native continuation-local
clear. The retained natural population has no final action-gate difference, but
the controlled gate-zero result below does.

Natural identify operations 305 and 376 both reach strong rescue with final
rejection evidence nonzero, but their captured templates have queue state
`+0x8e00 == 1`; the producer is therefore false independently of rescue ordering.
The focused identify-376 control changes only that state through the native
object/codec contract: unpack the original state-1 object, write live scalar
`+0x8e00 = 0`, pack, unpack again so the DLL allocates 20 owners with ranks `-1`,
then pack again. The second pack is exact. Relative to original SHA-256
`a0cd752af6f40970d8ef3f04b3cece20cd4d06ebc252541f651a6f28cbb72f29`,
state-0 SHA-256
`a12cd89ea3f96bda0e219581cdbc4f8308d4b54b44950738064b540ded77749f`
differs only at outer CRC bytes `1..4` and tag `fa` byte `0x42`. Feature count and
maximum are both 40, the queue counter remains zero, all 20 owners are allocated,
and occupancy is zero. Queue state is not a `FUN_18005d9e0` rescue input; static
inspection finds no `+0x8e00` access in that helper, and the serialized equality
outside CRC/`fa` preserves every rescue feature, order, mask, metric, and affine
input.

This full/state-zero shape is controlled-valid rather than malformed or merely
raw-patched. Production enrollment combine accepts up to 40 one-feature inputs,
zero-initializes `GoodixMilanTemplateMetadata` at
`enrollment/template.c:170`, sets maximum features to 40 at line 224, and packs
the complete metadata at lines 412..416 before full-gallery normalization. It
therefore produces queue state zero at ordinary enrollment capacity. The DLL
round trip independently proves that the corresponding native object owns the
required empty queue allocation.

The target and its untouched state-1 control use the same setup, live frame,
probe, gallery feature payload, and identify/study transaction. Results are:

| Authority/input | Queue before/after match/after study | Score/action | After-match SHA-256 | Final candidate SHA-256 |
|---|---|---|---|---|
| DLL state-1 control | `0/0/0` | `28/4` | `0bb0e23cc4b120aa8af1561ce98ee9888b1897c3770012b518e0492571bd5c43` | `e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350` |
| Base commit `c182b140` state-1 control | `0/0/0` | `28/4` | `0bb0e23cc4b120aa8af1561ce98ee9888b1897c3770012b518e0492571bd5c43` | `e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350` |
| DLL state-0 target | `0/0/0` | `28/4` | `b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83` | `e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350` |
| Base commit `c182b140` state-0 target | `0/1/0` | `28/5` | `b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83` | `2a8be296bd58df5d5cf7ef2e8335d5506603f44b93334b81548a0cfb748bbcd5` |
| Working-tree correction, state 1 | `0/0/0` | `28/4` | `0bb0e23cc4b120aa8af1561ce98ee9888b1897c3770012b518e0492571bd5c43` | `e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350` |
| Working-tree correction, state 0 | `0/0/0` | `28/4` | `b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83` | `e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350` |

All state-zero runs preserve accepted score 28 and the exact packed after-match
object because queue bodies/ranks are not serialized. The DLL sees post-rescue
rejection and does not enqueue. Base commit `c182b140` consumes its stale
pre-rescue eligibility, deep-enqueues one entry, consumes it during the same
study transaction, overrides primary action 4 with action 5, and publishes a
different final candidate. The working-tree correction is exact to the DLL for
both state controls. This confirms the generic correction boundary, but
raw rescue `set_rejection` is not the native suppression predicate. The
working-tree correction on production head `aa5ad6c6` instead computes the
shared effective rejection after rescue, uses it for queue admission and the
study action gate, and applies the same retention owner to direct-positive
continuation.

The native correction boundary is one effective post-rescue rejection value:

```text
effective_rejection = traversal_rejection or rescue_rejection
if not rescue_rejection_retention_gate:
    effective_rejection = false

queue_candidate_eligible = queue_predicate(effective_rejection, ...)
study_action_gate = effective_rejection
```

Recomputing the complete queue predicate is required: a zero gate clears all
`+0x688` evidence, not only the most recent rescue write, so toggling a stale
pre-rescue Boolean cannot model every route. The gate's `0x180056f88` consumer
also exits traversal after a direct-positive publication. The working-tree
driver correction applies both consumers, while intentionally specializing the
initial gate to the only `c0` value its matcher entry can receive.

When the retention gate is zero, `0x180056cba` clears rescue rejection. Both
focused production-shaped strong-rescue controls retain rejection: state-zero
identify 376 remains native
score/action `28/4`, queue `0/0/0`, and state-zero
`da0f8d98-6e9e-4cee-99d5-f7109e402baf/identify/305` remains `26/4`, queue
`0/0/0`. Raw `set_rejection` does not always survive by contract. A
controlled-valid identify-376 matcher input with only live probe `c0=2` makes
`FUN_18005e480` return zero, initializes the retention gate to zero, and clears
the rescue write before the otherwise-true queue checks. Ordinary image
extraction supplies `c0=0`, so this initial-gate control is not a naturally
extracted probe and is not a production-support claim. It records the DLL's
broader contract and proves why raw rescue rejection alone was not a valid
replacement for the shared native state.

The other initial clear is driver-representable without `c0`: packed probe
`c7=0x500` makes `FUN_180073060` decode high mode 9. Current extraction can
produce high class 5 at `match/info.c:214..243`, and then stores that class in
`optional_c7` at lines 535 and 620. No retained natural operation has this value,
so frequency is unassigned. Its accumulated high class also makes the `<4` queue
predicate false independently, but the gate still controls final action evidence
and direct-positive continuation. A focused current-runner control with
`optional_c7=0x500` returns score/action `28/0`, queue `0/0/0`, no candidate,
and after-match SHA-256
`b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83`.
This agrees with the recovered mode-9 ownership; no reliable paired DLL artifact
exists for an exact dynamic comparison.

The approved DLL replay of that `c0=2` control returns score 28, update 0, queue
occupancy `0/1/1`, after-match and after-study SHA-256
`b7de86fff2c1404bfcabd899dcc9d7480949f419509fbbcb653d9d59f9a7fb83`,
and no persistent candidate. Gate zero clears rescue rejection, permits the
enqueue, publishes action evidence zero, and therefore prevents `templateStudy`
from entering the queue consumer. The discarded raw patch instead returns
score/action `28/4`, occupancy `0/0/0`, and candidate
`e868db161c012c00088bb686e90d4032c5fc29f0c3a646b2bf570cf96f05c350`.
It suppresses the queue and leaves the independently computed action gate set,
which proves the patch too broad even before considering direct continuation.

No focused production image control reaches the separate late one-shot clear,
but it is statically reachable. Six valid processed features can each make
`FUN_180058700` return nonzero, incrementing `[rbp-0x54]` and continuing the
loop. On the next valid feature, status count 6, accumulated high class below 5,
and zero `[rbp-0x48]` satisfy `0x180055e12..0x180055e2e`: native increments the
high class, latches the one-shot, and clears `[rbp-0x50]`. Starting from high
class zero leaves the resulting class one compatible with the later `<4` queue
condition. Status-producing rows are skipped before rescue-row publication, so
the recovered control flow contains no structural exclusion between this clear
and a later strong rescue. This establishes reachability of the branch, not a
natural-frequency claim.

The second is a direct result followed by terminal blocker override:

```text
direct_positive_feature_mask != 0
and ((blocker_count > 5 and blocker_sum > blocker_count * 60)
     or (blocker_count > 10 and blocker_sum > blocker_count * 50))
```

For this conjunction, the DLL has already incremented every direct feature when
`0x180057109` replaces the final score with `-65536`. Current source creates the
direct slot and lifecycle bit in `match/selection.c:333..340`, records blocker
count/sum in `match/selection.c:386..389`, publishes the lifecycle mask before
the terminal score override in `match/match.c:1702..1714`, and then
`match/lifecycle.c:126..141` applies that mask only when final `score > 0`.
Consequently the two serialized after-match objects would differ by the earned
`be` increments and dependent CRC. Rescue cannot participate because its caller
requires blocker count zero.

Blocking and direct publication are mutually exclusive only within one
traversal occurrence. A blocking hit continues the feature loop without
clearing prior direct score or mutation, and a direct occurrence does not clear
prior blocker count/sum. Distinct physical features can therefore satisfy the
static conjunction; no recovered producer invariant proves it impossible.

The strict readiness-v3 population contains 701 operations and 1,104 gallery
rows. It has 592 rows with a nonzero captured lifecycle mask and one row with
score `-65536`, but zero rows with both. The blocker row is natural
`0e385ab2-63b3-4891-aa1c-6b6af49b9154/identify/315`, gallery position 1: its
captured lifecycle mask is zero and its input and after-match SHA-256 are both
`aae64e61b404f6c87d70b38eeb01e91b44c6942a508915ef7ef09a3231ae35d3`.
Natural direct control `identify/329`, position 0, publishes score/mask/feature
`22/0x10000000/28` and after-match SHA-256
`8a7a5e6f02c03fbfc06ab964533992363eab52c5969f42685f8d1193addecaa2`.
At commit `c182b140`, strict current/native replay of both complete operations is
exact for score, status, after-match bytes, queue, candidate, and actual study
lifecycle (`selected=2, passed=2, failed=0`). The report is
`<parity-state>/reports/direct-blocker-lifecycle-followup-20260820.json`,
SHA-256
`26b6632ff0b78be779618fd3eca9b02c1fcd00bb87cb0aab6db9cb4d935df0ce`.

### Direct-plus-blocker reachability boundary

The natural `identify/315` result is not the direct-then-blocker conjunction. Its
one blocking occurrence has adjusted detail 69, so the blocker state is
`count=1,sum=69` and the ratio override is false. With no direct score and no
eligible aggregate or fallback publication, the terminal fallback-rejection
path maps any nonzero blocker count to `-65536`. Score `-65536` by itself is
therefore not evidence for the lifecycle boundary.

The exact target requires at least seven distinct physical features in a valid
unique `a1` permutation: one direct feature plus six blocking features whose
adjusted-detail sum is strictly greater than 360. The alternate branch requires
one direct feature plus 11 blockers whose sum is strictly greater than 550. The
six-blocker branch remains applicable above ten blockers when its stricter
average is met. Every blocker has already contributed to the Q8 prefix, but its
occurrence skips retained/direct admission. The direct occurrence may precede
or follow the blockers because neither side clears the other's match-local
state. Rescue cannot manufacture the conjunction because nonzero blocker count
makes its caller ineligible.

The static DLL chain is complete for this boundary:

1. `FUN_18003a3e0`, called at `0x18005670e`, compares the shared probe
   anti-fake object with the current enrolled physical feature's anti-fake
   object.
2. A hit writes match-local blocker count/sum at
   `0x180056c22..0x180056c26` and continues to the next traversal occurrence.
3. A different nonblocked direct occurrence increments its physical feature's
   packed `be/+0x134` at `0x180056f72` and latches a positive score.
4. Post-loop ratio evaluation at `0x180056d54..0x180056d75` observes both
   owners, and `0x180057109` may replace only the score with `-65536`.
5. `identifyImage` rejects the gallery row by signed score, but a subsequent
   `templatePack` at `0x1800020b0` still serializes the earlier `be` mutation.

The DLL owns no direct or lifecycle mask. For diagnosis, current source's
physical masks must be paired with native packed-byte evidence. A credible
witness has a nonzero direct/lifecycle mask in current diagnostics, blocker
count/sum satisfying one strict ratio above, terminal score `-65536`, and a
native after-match object whose direct owners' `be` values and dependent outer
CRC differ from input. Selected physical feature, direct affine, active routed
relation, and rejection evidence may remain populated inside the rejected row;
the outer identify winner remains the sentinel and study is not attempted. The
queue predicate is evaluated independently before terminal override, so queue
state must be reported rather than inferred from the negative score.

### Natural controls and lineage provenance

The blocker control's immutable capture record is
`58a70be83891e6efb635856b8645e0100d15d195865be305b08d21344d736e88`.
Probe `007ae5bf68d90bbf893ed9d2dae298bf6010c381e0bb425a107c66c955f5c696`
has quality/coverage `52/41`. Against gallery-position-1 template
`aae64e61b404f6c87d70b38eeb01e91b44c6942a508915ef7ef09a3231ae35d3`,
one-based traversal occurrence 28 addresses physical feature 15 and is the only
contributor. Its metrics are
`[5,5,0,0,222,203,201,109,191,73,34,14,0,1,0]`, direct affine is
`[257,-54,-3872,25,245,-7887]`, anti-fake deltas are texture/shape/boundary
`-86/446/363`, boundary score and candidate coverage are `95/73`, and pair
metrics are `[0,5,1321,2587,89]`. The boundary adjustment is `-20`, producing
blocking metric 69. Feature 15 is active with `b6=106`, graph reference zero,
and `be=3`; relation slot 106 is `[0,254,-31,8375,31,254,-4142]`. The block
prevents direct admission, input equals after-match, and no study runs.

The direct control's immutable capture record is
`7d4c4505dda2434cd6072fa62839c14b94142f2a8a575a100081f3911068016b`.
Probe `72060d07980f0133190a10c288353b1daf526eccb556d1e9d36652fb1e51a1ac`
has quality/coverage `52/72`. Against the separate gallery-position-0 lineage
template `5a00a6c7d9f66b26b7dcc028c96dbe8b8efb252ea6004676e946479c178575d9`,
physical features 31, 39, 15, and 28 contribute Q8 terms `37,49,30,110` in
traversal order. One-based occurrence 23 addresses physical feature 28 and
publishes direct from metrics
`[9,18,0,0,222,203,212,103,206,155,52,21,0,0,0]` and affine
`[262,-42,5778,50,246,2592]`. The final Q8 state is `226/4`, yielding score 22.
Feature 28 is active with `b6=379`, graph reference one, and relation slot 380
`[0,246,72,20286,-72,246,-21637]`; its routed relation is
`[0,255,27,26567,-27,255,-20772]`. No occurrence blocks. Native mutation is
feature 28 `be:6->7` plus outer CRC, represented in current diagnostics as mask
`0x10000000`. This control must not be spliced into the blocker lineage.

Admissible search provenance is tiered:

| Tier | Admissible construction |
|---|---|
| 1 | Exact captured native probe paired with an exact native `templateStudy` output from the same enrolled-print lineage; retain operation selector, capture-record hash, input/output hashes, winner position, and action for every lineage edge |
| 2 | A native pack/unpack-canonical template reached only through native-supported study actions using natural probes and features from that same lineage; log every input hash, probe hash, selected action, and native output hash |
| 3 | Manual feature splicing, arbitrary internal-field changes, cross-lineage features, patched anti-fake objects, or synthetic `a1` permutations; never admissible as a real-usage witness |

The exact gallery-position-1 Tier-1 chain around the blocker is:

```text
identify/313: fd6ff779... --action 4--> aae64e61...
identify/315: aae64e61... --blocker rejection, no study-->
identify/316: aae64e61... --action 4--> 1e67b128...
identify/322: 1e67b128... --action 0, no candidate-->
identify/324: 1e67b128... --action 4--> 6c7a7fe5...
identify/326: 6c7a7fe5... --ordinary rejection, no study-->
identify/327: 6c7a7fe5... --action 4--> 50c1891e...
identify/331: 50c1891e... --action 4--> 163a4798...
```

Gallery position 0 is a different enrolled-print lineage:
`f435d79d... -> 04642678... -> 5a00a6c7... -> dacd6425...`. Stable gallery
position and winner/candidate handoff prove each chain; they do not authorize
cross-chain probe/template combinations.

### Structural reachability

The direct and blocker classifications are feature-local evaluations under one
shared probe. Each enrolled physical feature independently owns its recognition
records, anti-fake object, active state, packed coverage, optional `c7`, graph
reference, and candidate transform. Candidate metrics and final flags are
computed from that feature's records against the shared probe records. The
anti-fake call then compares that feature's anti-fake object with the shared
probe anti-fake object under the same feature-local transform. No recovered
writer equates one feature's anti-fake result, direct flag, transform, or
lifecycle state with another feature's result.

Probe-global inputs are the probe records and anti-fake object, image
quality/coverage, packed probe mode, policy configuration, and mutable late
policy state. Match-global owners are the Q8 sum/count, winner rank,
acceptance/rejection evidence, retained-active evidence, blocker count/sum,
queue state, and accumulated late class/status. These introduce ordering but no
direct-versus-blocker exclusion across features:

- Every blocker must first pass candidate construction, mutable policy, and the
  contribution predicate. It contributes one Q8 term, then skips admission, so
  it does not publish direct or retained-active state.
- Six blockers can precede the direct feature in `a1`. Because blocking skips
  admission, those occurrences do not create the retained-active plus rejection
  state used by the full-gallery active-feature skip. Placing direct last avoids
  that skip even in a 40-feature gallery.
- All blockers require ordinary blocking mode, both anti-fake objects, shared
  probe coverage above 40, and each enrolled feature's packed coverage above 40.
  Natural controls satisfy these ranges; no extreme or malformed value is
  needed.
- Enrolled feature `c7 == 0`, observed throughout the authoritative valid
  population, leaves accumulated late class unchanged. Per-occurrence mutable
  policy may still reject a candidate, but it does not couple a successful
  blocker to another feature's direct flag.
- Nonzero blocker count suppresses rescue and no-direct aggregate publication.
  It does not suppress a direct score already latched by a later feature.

A valid profile-9 template therefore has sufficient capacity: six distinct
blocking physical features plus one distinct direct feature use seven of the
maximum 40 slots. The conjunction is structurally reachable by an ordinary
probe/gallery transaction. This is a reachability statement, not a frequency
claim. The natural blocker and direct controls came from different probes, but
all composed values are ordinary feature-local outputs already produced by the
DLL; the function has no shared invariant requiring their source probes or
features to remain paired.

### Aggregate ratio-blocker control

A bounded finalization control composes the minimum state from the natural
per-feature records above. Physical features 0 through 5 each use the blocker
metrics, affine, anti-fake deltas, pair metrics, Q8 term 30, and adjusted blocker
metric 69 from `identify/315`. Physical feature 28 uses the direct metrics and
affine from `identify/329`, contributing Q8 term 110 and direct/lifecycle mask
`0x10000000`. The coherent post-loop state is therefore Q8 `290/7`, positive
latched score 16, blockers `6/414`, and one direct owner. A boundary control
changes only blocker sum to 360.

The control executes production `selection.c` unchanged and injects the same
coherent post-loop owners into the approved DLL and exact current runner at their
finalization boundaries. Results are:

| Case | DLL score | Current score | Direct/lifecycle owner | DLL feature 28 `be` | Current feature 28 `be` | DLL after-match | Current after-match |
|---|---:|---:|---:|---:|---:|---|---|
| Target `6/414` | `-65536` | `-65536` | `0x10000000` | `6->7` | `6` | `8a7a5e6f02c03fbfc06ab964533992363eab52c5969f42685f8d1193addecaa2` | `5a00a6c7d9f66b26b7dcc028c96dbe8b8efb252ea6004676e946479c178575d9` |
| Boundary `6/360` | `16` | `16` | `0x10000000` | `6->7` | `6->7` | `8a7a5e6f02c03fbfc06ab964533992363eab52c5969f42685f8d1193addecaa2` | `8a7a5e6f02c03fbfc06ab964533992363eab52c5969f42685f8d1193addecaa2` |

The target DLL input is
`5a00a6c7d9f66b26b7dcc028c96dbe8b8efb252ea6004676e946479c178575d9`.
Its packed delta is exactly outer CRC bytes at one-based offsets 2 through 5 and
feature 28 `be` at one-based offset 357960, `6->7`. Queue occupancy remains zero
and study is not called on the negative result. The equality control proves the
sum comparison is strict and isolates the ratio override from fallback's
independent any-blocker `-65536` route.

The DLL hook records direct increment at `0x180056f72`, injects Q8/count and
blocker count/sum at `0x180056d54`, and observes final score at `0x180057111`.
The transient oracle executable SHA-256 is
`6346c162b00b7907c16cf55600f66bdd8989cfe0dac9f05460742c24a8cf14bc`.
The current debug runner source identity is
`de4629d676889fbaa9bd4365b7ee1cf5672bb8d1bb1d03996625bf2f480653cf`,
backend SHA-256
`2d66a669c96d8764b2ca4a7216ddf55700b5be1628758c94537522429b5bad5d`.

Confounders excluded by the control are fallback's any-blocker rejection,
contributor bits mistaken for direct bits, one occurrence counted in both roles,
graph references mistaken for physical indices, rescue selection, queue
mutation, canonicalization-owned `bb/a1` changes, CRC-only changes, and the outer
winner sentinel interpreted as loss of row-local direct state.

## Exact Publication Witnesses

Three strict runtime-debug/v3 operations cover both natural 40-feature galleries,
nonphysical `a1` permutations, and probe packed modes `0`, `0x100`, and `0x200`.
Gallery 0 has 38 active features and graph reference 1; gallery 1 has 39 active
features and graph reference 0.

| Selector suffix | Probe mode | Gallery 0 | Gallery 1 | Overall |
|---|---:|---|---|---|
| `identify/300` | `0` | reject, score `-4` | accept, score `45` | winner row `1`, action `4` |
| `identify/305` | `0x100` | reject, score `-7` | accept, score `26` | winner row `1`, action `4` |
| `identify/316` | `0x200` | reject, score `-4` | accept, score `52` | winner row `1`, action `4` |

Current source and the approved DLL are exact for all six gallery rows: score,
acceptance, after-match bytes, queue occupancy, overall winner, final candidate,
study action, and attempted/completed lifecycle outputs. Validation summary is
`selected=3, passed=3, failed=0`; all 698 nonselected dump operations were left
untouched. The private report is
`<parity-state>/reports/matcher-orchestration-c182b140-fresh-3-20260820.json`,
SHA-256
`919a34071f509f90d892a7519fbd96458bc262524fe748a3b61c6dffd4ee6caf`.

Focused rows close the remaining publication routes:

| Route | Exact result and bytes |
|---|---|
| Direct, `identify/329` row 0 | score/selected/lifecycle `22/28/0x10000000`; input `5a00a6c7d9f66b26b7dcc028c96dbe8b8efb252ea6004676e946479c178575d9`; after-match `8a7a5e6f02c03fbfc06ab964533992363eab52c5969f42685f8d1193addecaa2`; only CRC and feature 28 `be:6->7` change |
| Direct then rescue, `identify/305` row 1 | score/selected/lifecycle `26/8/0x200000000`; input `c255f44dd1d188045ca0db38dbfabf78e4697fe2dee2ce28464a0108c684f589`; after-match `73ea08e33ce7fa11e5cc9160951bf0b7549ebe434f799a329ecc891cbf798c31`; only CRC and earlier direct owner feature 33 `be:16->17` change |
| Rescue only, `verify/180` | score/selected/lifecycle `19/13/0`; input and after-match `aa96c86d97b7627a22a2582be0cd50fcbf82e7f28c8ec0167371159e748d353b`; queue remains empty |
| Fallback rejection, `identify/326` | rows score `-4/-3`, sentinel selection, lifecycle zero, queue `0->0`; both after-match hashes equal their inputs: `5a00a6c7d9f66b26b7dcc028c96dbe8b8efb252ea6004676e946479c178575d9` and `6c7a7fe5015ff4b51133acd8d657b5da091a3b282b77758b8e555a0d54d76992` |
| Queue enqueue, `identify/11` row 0 | score/selection/lifecycle `-4/-1/0`, queue `0->1`; input `7dbebdcf672e8d0be38dfbefc39be217189d078c65354fa531e8445d76884860`; normalized after-match `001112706e1bcd51c679482d8ab0f712ea6d6b99a69c2e7b42c43035ea0ea0bf` |

The queue row's byte delta is CRC plus normalized `bb`: feature 0 `0->365`,
feature 2 `0->592`, features 3 through 5 `0->2288`, feature 6 `0->212`, and
features 7 through 11 `0->2288`. It has no lifecycle owner.

The frozen passive no-direct aggregate `157/2` supplies the contributor-only
route. It publishes score/selected/direct/contributor/lifecycle
`26/-1/0/0x80/0x80`. Loaded SHA-256 is
`4ce1798591feb378e9d27fa2a5d30a137e0745cd7b80f33fd6ba40ce60b7943b`;
packed after-match SHA-256 is
`4f2bd1ec7a8794f34f3e1c6b7201cda7660aab8f7d4f03ce9b38c5c592971d7f`.
Normalization changes all eight `bb` values to
`[841,73,236,2288,43,411,268,2288]` and `a1` from
`[0,1,2,3,4,5,6,7]` to `[6,5,4,2,1,0,7,3]`; lifecycle changes only feature 7
`be:0->1`. Primary and repeat traces are byte-identical under
`<evidence-root>/derived/milan-native-capture-20260723/`
`match-lifecycle-contract-v1/traces/immutable-aggregate-e2-seq157-*`.

The focused four-operation current/native report has SHA-256
`dde0683454174ff2573b31127aeeaa864289853443705249bee9c91b0c390240`,
with `selected=4, passed=4, failed=0`. The rescue-only report has SHA-256
`44a9ba1c6d5aadffacda4a7d9472400678427fea8fee52eec6059b54491bf825`,
with `selected=1, passed=1, failed=0`.

These witnesses contain no final-state or packed-byte difference. The exact
unwitnessed predicates above, the loop-top feature-`c7` ordering boundary, and
queued policy provenance remain bounded valid-state differences. DLL queued
word 18 is zero and word 19 is preexisting/carry state; current source uses each
queued probe's packed mode and recognition one. The strict corpus has no
action-5 row and maximum observed queue occupancy one, so it cannot exercise a
committed second queued matcher call.

## Cross-References

- `re/milan/functions/FUN_180055a40.md`: complete function evidence and leaf
  dependency history.
- `re/milan/functions/FUN_18005edb0.md`: ordinary family dispatch and live-probe
  ownership.
- `re/milan/functions/FUN_18005d330.md`: queued caller configuration and gallery
  provenance.
- `re/milan/functions/FUN_18005e230.md`: policy initialization and configured
  physical-index override.
- `re/milan/functions/FUN_18005e3e0.md`: evidence-object initialization.
- `re/milan/functions/FUN_18005ef60.md`: unresolved-reachability combined
  match-and-study composition.
- `re/milan/functions/FUN_1800619a0.md`: graph-reference routing arithmetic.
- `re/milan/OUTER-TEMPLATE-CODEC.md`: physical feature, graph, and `a1` wire
  ownership.
