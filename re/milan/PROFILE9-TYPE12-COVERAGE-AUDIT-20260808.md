# Profile 9 / Type 12 Coverage Audit (2026-08-08)

## Scope And Authority

This is a read-only coverage audit of the frozen enlarged corpus at:

`/home/anthony/dev/goodix-fp-dump/analysis/milan-profile9-v3-expanded-20260807T225741Z`

It is bounded to sensor subtype 12, which selects profile 9. Static claims refer
only to `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
No behavior from another profile or subtype is used to fill a coverage gap.

The snapshot has 334 runtime records: 34 enrollment, 214 identify, and 86
verify. The authentication coverage set below contains the 299 identify/verify
operations with galleries: 213 identify and 86 verify. The additional identify
operation, selector
`0b4a03ea-e3f9-46cc-bdf8-426d7a50375f/identify/151`, is a real raw-admission
retry and has no valid processed image, probe, candidate, or gallery lifecycle.
It is counted only in raw-admission coverage.

Evidence classes used below are:

- **Real**: naturally reached by a frozen corpus operation.
- **Control**: established by a focused direct or synthetic-valid DLL/native
  control, but not naturally reached by the 299 gallery operations.
- **Static**: established from the relevant DLL path, without an input-exact
  profile-9/type-12 execution that reaches the distinction.
- **Uncovered**: neither a natural row nor a documented focused control proves
  the complete producer or state transition.

The named pinned-current-backend and strict-retry report JSON files were not
present at the paths searched during this audit. The maintained function notes
preserve their conclusions. The identify-151 authority is the maintained strict
report `expanded-225741-identify151-preprocess-final.json`; this note does not
re-seal or reconstruct a missing report.

## Corpus Dimensions

| Dimension | Frozen real coverage | Control/static coverage | Residual classification |
| --- | --- | --- | --- |
| Authentication lifecycle | 299 gallery operations: 268 accepts and 31 no-matches | Raw retry is separately represented by identify 151 | Broad ordinary lifecycle coverage; retry must not be projected as extraction |
| Operation kind | 213 identify and 86 verify with galleries | Enrollment records exist outside this 299-operation table | Both authentication entry points represented |
| Status and score | Status values 0 and 1; scores span `-7..97` | Exact negative fallback component and positive publication controls exist | Real score diversity does not imply every matcher route |
| Gallery shape | 122 galleries of size 1 and 177 of size 2; positions 0 and 1 | Larger-gallery matcher behavior is covered by prior authorities and focused controls, not this frozen set | Frozen set is limited to one/two gallery candidates |
| Quality and coverage | Quality `18..98`; coverage `14..100`, including 75 | Strict threshold controls exist where documented | Good scalar range, but not branch-pair coverage |
| Gallery evaluation | 476 rows, all valid/evaluated: 268 accepted and 208 rejected | Accepted lifecycle masks and selected features are concrete on all accepted rows | Strong ordinary candidate evidence |
| Study action | Action 0: 16; action 1: 56; action 4: 196; null on 31 no-matches | Actions 2, 3, and 5 have focused DLL/native controls | Natural frequency is known only for 0/1/4 |
| Queue | Frozen Linux records report 135 match-time enqueues, but later no-injection DLL replay supersedes those records as native producer evidence; current-schema standing sets have zero match-time occupancy | Preoccupied, multi-entry, no-match follow-up, and action-5 success have focused consumer controls | No natural preoccupied queue or queued action 5; native producer frequency remains unestablished |

## Native-Branch Coverage

| Native boundary | Real coverage | Control/static coverage | Gap |
| --- | --- | --- | --- |
| `FUN_180069820` raw clamp/admission | All selected raw samples are at most `0x0fff`; identify 151 naturally reaches `0x29aa` at 1,251 passing interior samples; identify 19 passes at 1,386 | Exact 1,310/1,311 live and 7,425/7,426 setup controls are documented | No corpus row reaches setup `0x29bb`; its complete sample-failure ownership is control-only |
| `FUN_180069820` validated border path | Profile-9 live path and non-inversion polarity are real | Validated/nonvalidated border distinction has exact controls | Over-range interaction with border copy remains unexecuted |
| `FUN_18006b290` mode-9 severe result | No physical row returns `0xc351` | An approved-DLL setup/live boundary control returns `0xc351`, quality/coverage `0/12`, and populated output for both purposes; adapter status handling is exact | Physical prevalence is unknown |
| `FUN_180048260` packed extraction class | Probe `c7` is only `0`, `0x100`, or `0x200`: 64/68/167 operations. The low class is always zero and high class is only 0/1/2 | `FUN_180070d90` controls all 12 raw decoder values | Nonzero low classes and produced high classes 3/4/5 are absent naturally |
| `FUN_180048260` mode/output plane | Ordinary v3 probes select mode 0 and expose zero in the three defined summary bytes | An ordinary-selector focused chronology proves mode 1 is the first producer-reachable nonzero mode and defines summary `[1,0,0]`; the strict native width-230 mode-zero boundary has a complete matching witness | Mode 2 and projected modes 3/4/5 remain without focused producer/output execution |
| Extraction history selector 2 | Frozen artifacts do not expose a selector-2 transition, prior-high reuse, or the inverted prior-coverage admission predicate | The complete scalar/ring behavior is static; ordinary WBF callers force the selector-producing argument to zero | Direct exported mode only, not an ordinary-operation coverage gap |
| Serialized feature classes | Across 1,503 probe/gallery/candidate artifacts, all 40,697 feature elements use only `0`, `0x100`, or `0x200`; galleries/candidates contain no `0x200` | Downstream decoder and matcher-state controls cover valid class values | The real artifact manifold is much narrower than the valid packed-class domain |
| `FUN_1800392f0` anti-fake output | Candidate count `3..100` across 66 values; texture `103..295`; mean `1501..2050`; variation `0..402`; boundary `12..18`; model `-50295..47621`; 167 support-mask hashes | Canonical-zero one-past-byte policy has paired controls | Broad output variation does not cover every upstream impulse or allocation boundary |
| `FUN_18003a9b0` impulse replacement | Identify 59 proves physical last-column/next-row flat adjacency and exact downstream probe closure | In-allocation flat offsets and safe out-of-allocation fallback are static | No qualifying first/allocation-end impulse naturally distinguishes the safety fallback |
| `FUN_180058700` late policy | Natural authorities cover ordinary/status-zero, mode-4 status, area, and geometry examples; a valid captured gallery combined with an ordinary mode-1 probe crosses the state1-one status band | Focused unit 16 covers the former 55-branch caller model and 404 DLL calls; ordinary mode 1 makes a 56th state1-one margin arm reachable | The native state1-one margin-13 arm now has a complete operation witness |
| `FUN_18005d5f0` fallback | Real rejected rows cover all negative reason components and 36 fallback events | Positive, zero, negative, and publication controls cover final behavior | No natural positive fallback in the maintained authorities |
| Matcher queue/follow-up | No native natural preoccupied queue or successful queued follow-up is established; older Linux queue metadata is not producer authority | Deep-owned one/two-entry and action-5 consumer controls are exact | Natural reachability/frequency remains unknown |
| `templateStudy` actions | Natural actions are 0/1/4 only | Actions 2/3/5, packing, persistence-positive handling, and queue handoff are controlled | Natural action-2/3/5 examples remain absent |

## Raw-Admission Correction To The Frozen Projection

Identify 151 is the sole raw-admission failure in the enlarged set. Its 8,736
interior samples contain only 1,251 values satisfying
`100 < sample < 0x0ed8`, below the strict 15% requirement. The frozen Linux
metadata continued to quality/coverage `28/14` and extraction, but the DLL
returns `0x29aa` with quality/coverage `0/0` and no later artifacts. Those stale
Linux outputs are not evidence for low-quality extraction or matching.

The next-lowest accepted operation, identify 19, has 1,386 passing interior
samples and is the appropriate accepted-path control. The exact mathematical
boundary remains established by focused 1,310-fail and 1,311-pass controls.

## Prioritized Residual Gaps

1. **Setup admission, high confidence and unresolved physical prevalence.**
   Profile-9 `preprocessor_init` clamps and then requires strictly more than 85%
   of the 8,736-pixel setup interior in `(100, 0x0ed8)`. Exact approved-DLL
   controls fail at 7,425 with `0x29bb` and pass at 7,426. The adapter retries
   setup once with the same frame, then returns bad-capture status
   `0x80098008` and detail 10 from the completed sample without entering live
   preprocessing or extraction. No physical corpus row establishes prevalence.
2. **Mode-9 severe result, high confidence and synthetic-valid implementation
   risk.** `FUN_18006b290` returns `0xc351` after complete rendering for its
   severe class-3 predicate. The adapter suppresses extraction for both
   purposes. The exact control is valid but no physical row establishes
   prevalence.
3. **Extraction mode production and class production, medium-high producer
   risk.** A focused ordinary chronology proves the native mode-1 target and
   its summary/projection contract. The corpus still does not prove production
   of nonzero low classes, high classes 3/4/5, mode 2, or mode-specific 3/4/5
   `+0x158` projections.
4. **Allocation-end impulse safety distinction, medium safety risk and low
   parity priority.** The DLL can read outside the residual allocation for a
   qualifying endpoint impulse. The native clamped fallback is an intentional
   safety policy. The gap should not be closed by reproducing undefined memory
   access; only the deterministic policy needs to remain explicit.
5. **Natural positive fallback, queue occupancy, and actions 2/3/5, low semantic
   risk but unresolved prevalence.** Focused controls establish behavior. The
   enlarged corpus cannot support claims about real frequency or hardware
   reachability for those paths.

## Why Passing Parity Missed These Gaps

- Exact reports prove behavior only on the observed input manifold. The frozen
  manifold has no over-range samples, only three packed `c7` values, and no
  natural study actions 2/3/5.
- Aggregate row and artifact counts are not branch coverage. Anti-fake scalars
  and support masks vary widely while a specific impulse endpoint or extraction
  history selector remains untouched.
- Decoder coverage is not producer coverage. All valid packed classes can be
  consumed correctly under direct controls while ordinary extraction never
  emits most of them.
- Fresh-process captures hide unreset or partially serialized DLL state. The
  extraction ring, prior coverage, hysteresis, and prior high class require
  chronology-shaped controls.
- The original identify-151 Linux projection recorded post-gate artifacts that
  the DLL lifecycle never creates. Including them made apparent quality and
  extraction coverage broader than the authoritative native lifecycle.
- Focused controls and natural rows answer different questions. Existing
  controls close semantic implementation contracts for policy, fallback, queue,
  and study, but they do not establish natural prevalence.

## Action-4 Semantic Coverage

Action 4 is common in natural profile-9 campaigns but requires a full 40-feature
gallery and depends on matcher-owned selected, lifecycle, retained, and relation
evidence. It is bounded continual learning: replace a comparatively redundant
stored view with a useful accepted probe while preserving graph continuity and
recomputing complete-gallery residual coverage.

Selector `becea91e-eff1-497c-af41-67f4d1b6ce2e/identify/26` is the rare natural
counterexample showing that public acceptance and action-code coverage do not
prove those inputs exact. A feature with useful aggregate bitmap/detail evidence
but weak direct geometry was vetoed by the DLL's grouped first-veto term and
remained aggregate-only. A flattened translation made it direct-positive,
triggered the full-gallery active-feature skip, and changed selected owner 12 to
25. Both paths still accepted and selected action 4, but match-time lifecycle
and the learned candidate differed.

The higher-level contract, reasons for natural sparsity, operator-level coverage
guidance, and concrete action-4 audit backlog are maintained in
`IDENTIFY-26-ACTION4-SEMANTIC-AUDIT-20260808.md`. Exact Ghidra-backed predicate
and call-path evidence remains in `functions/FUN_18005c3b0.md`,
`functions/FUN_180053220.md`, `functions/FUN_180057e60.md`, and
`functions/FUN_180055a40.md`.

## Recommended Non-Code Controls And Captures

1. Retain the exact 7,425-fail/7,426-pass setup controls alongside the existing
   1,310-fail/1,311-pass live controls. Add physical setup observations before
   assigning hardware prevalence to `0x29bb`.
2. Retain the purpose-0/purpose-1 mode-9 control as a failed lifecycle with raw
   status `0xc351`, metrics `0/12`, populated preprocessing output, and no
   extraction. Seek a physical witness before assigning hardware prevalence.
3. Capture one fresh-process extraction-history sequence that crosses retained
   counts 0 through 3 and coverage 74/75, then invokes auxiliary selector 2 with
   prior high above current high. Record packed `c7`, retained count/planes,
   prior coverage, and current/next-call effects.
4. Use focused direct controls, not corpus substitution, to exercise modes
   3/4/5 and every producible low/high class. Compare the defined three summary
   bytes and full projected plane only on modes that define it; do not compare
   allocator residue for modes 0/1/2.
5. Keep the allocation-end impulse behavior as a documented safety divergence.
   If a natural frame reaches it, retain the input and native deterministic
   output, but do not execute or bless the DLL's out-of-object load as required
   parity.
6. Continue to label positive fallback, preoccupied/multi-entry queue, and study
   actions 2/3/5 as control-only until a non-forced profile-9/type-12 lifecycle
   reaches them. Do not manufacture natural-frequency claims from direct calls.
7. Keep identify 151 in the operation ledger as a raw retry with nullable
   artifacts. Exclude its stale frozen Linux processed/probe fields from all
   extraction, anti-fake, matcher, queue, and study coverage counts.
8. Keep early validation retries distinct from the later stateful retry. The
   controlled `0x7531` witness returns quality/coverage `0/18`; runtime evidence
   and parity projections must preserve those metrics rather than applying the
   `0x29aa`/`0x29bb` zero-metric rule.
9. For action-4 campaigns, fill templates naturally to 40 features and vary
   contact pose, rotation, lateral placement, pressure, and partial coverage.
   Classify resulting captures by aggregate-only/direct-positive routing,
   full-gallery continue/skip behavior, selected/relation owners, and study
   target after capture; do not ask operators to target metric thresholds.

## Audit Verdict

The enlarged corpus is strong evidence for ordinary profile-9/type-12
authentication, score/outcome diversity, one/two-candidate gallery evaluation,
anti-fake output variation, negative fallback formation, and natural study
actions 0/1/4. Existing focused controls substantially extend semantic coverage
for late matcher policy, fallback publication, queue handling, decoder values,
and study actions 2/3/5.

It is not complete producer or state-machine coverage. Setup `0x29bb` and
mode-9 rejection remain absent as physical rows even though focused controls
establish their semantics. Extraction modes 2..5 and allocation-end impulse
behavior remain producer gaps. The first nonzero mode 1 now has a focused
ordinary producer/output case, and the native state1-one overlap margin has a
complete operation witness. The strict native width-230 classifier boundary
also establishes the complete native mode-zero score,
gallery, and persistence path. Those gaps explain how strict parity can be
correct for every frozen operation while still missing relevant native
behavior.
