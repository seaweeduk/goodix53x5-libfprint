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
| Queue | Match-time occupancy before study is always zero; 135 gallery evaluations enqueue one item; accepted study drains to zero | Preoccupied, multi-entry, no-match follow-up, and action-5 success have focused controls | No natural preoccupied queue or queued action 5 in the 299 |

## Native-Branch Coverage

| Native boundary | Real coverage | Control/static coverage | Gap |
| --- | --- | --- | --- |
| `FUN_180069820` raw clamp/admission | All selected raw samples are at most `0x0fff`; identify 151 naturally reaches `0x29aa` at 1,251 passing interior samples; identify 19 passes at 1,386 | Exact 1,310/1,311 strict-admission controls and retry retention are documented; clamp-before-admission order is static | No corpus row distinguishes clamp from reject for an over-`0x0fff` setup/live sample |
| `FUN_180069820` validated border path | Profile-9 live path and non-inversion polarity are real | Validated/nonvalidated border distinction has exact controls | Over-range interaction with border copy remains unexecuted |
| `FUN_180048260` packed extraction class | Probe `c7` is only `0`, `0x100`, or `0x200`: 64/68/167 operations. The low class is always zero and high class is only 0/1/2 | `FUN_180070d90` controls all 12 raw decoder values | Nonzero low classes and produced high classes 3/4/5 are absent naturally |
| `FUN_180048260` mode/output plane | Ordinary v3 probes select mode 0 and expose zero in the three defined summary bytes | Modes 1/2 share the summary-only path; modes 3/4/5 and their projection behavior are statically recovered | No documented focused producer/history control proves a nonzero mode transition or projected output bytes |
| Extraction history selector 2 | Frozen artifacts do not expose a selector-2 transition, prior-high reuse, or the inverted prior-coverage admission predicate | The complete scalar/ring behavior is static | Uncovered as an input-exact stateful sequence |
| Serialized feature classes | Across 1,503 probe/gallery/candidate artifacts, all 40,697 feature elements use only `0`, `0x100`, or `0x200`; galleries/candidates contain no `0x200` | Downstream decoder and matcher-state controls cover valid class values | The real artifact manifold is much narrower than the valid packed-class domain |
| `FUN_1800392f0` anti-fake output | Candidate count `3..100` across 66 values; texture `103..295`; mean `1501..2050`; variation `0..402`; boundary `12..18`; model `-50295..47621`; 167 support-mask hashes | Canonical-zero one-past-byte policy has paired controls | Broad output variation does not cover every upstream impulse or allocation boundary |
| `FUN_18003a9b0` impulse replacement | Identify 59 proves physical last-column/next-row flat adjacency and exact downstream probe closure | In-allocation flat offsets and safe out-of-allocation fallback are static | No qualifying first/allocation-end impulse naturally distinguishes the safety fallback |
| `FUN_180058700` late policy | Natural authorities cover ordinary/status-zero, mode-4 status, area, and geometry examples | Focused unit 16 covers all 55 reachable profile-9/type-12 semantic branches and 404 DLL calls | No semantic implementation gap; most branch frequency is control-only |
| `FUN_18005d5f0` fallback | Real rejected rows cover all negative reason components and 36 fallback events | Positive, zero, negative, and publication controls cover final behavior | No natural positive fallback in the maintained authorities |
| Matcher queue/follow-up | No natural preoccupied queue or successful queued follow-up in this set | Deep-owned one/two-entry and action-5 controls are exact | Natural reachability/frequency remains unknown |
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

1. **Over-range clamp execution, high confidence and high implementation risk.**
   The DLL clamps before admission and does not reject a frame merely because a
   setup, border, or interior sample exceeds `0x0fff`. No frozen raw frame has an
   over-range sample, so whole-frame parity can pass while a reject-instead-of-
   clamp implementation remains wrong.
2. **Extraction selector-2/history sequence, high state risk.** The selector
   reuses the prior merged high class and reverses the history admission test to
   depend on prior retained coverage below 75. Those globals are not fully
   serialized, and the 299 operations expose no input-exact transition. Fresh-
   process output parity therefore does not prove service-lifetime behavior.
3. **Nonzero extraction modes and class production, medium-high producer risk.**
   Decoder and downstream policy controls prove how valid classes are consumed,
   but the corpus does not prove production of nonzero low classes, high classes
   3/4/5, nonzero modes 1/2, or mode-specific 3/4/5 `+0x158` projections.
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

## Recommended Non-Code Controls And Captures

1. Add a sealed direct DLL control matrix for setup and admissible live frames
   with a single over-`0x0fff` sample at an interior position, outer border, and
   second-from-edge position. Record clamp, admission result, border output,
   quality/coverage, and retry lifecycle separately.
2. Capture one fresh-process extraction-history sequence that crosses retained
   counts 0 through 3 and coverage 74/75, then invokes auxiliary selector 2 with
   prior high above current high. Record packed `c7`, retained count/planes,
   prior coverage, and current/next-call effects.
3. Use focused direct controls, not corpus substitution, to exercise modes
   3/4/5 and every producible low/high class. Compare the defined three summary
   bytes and full projected plane only on modes that define it; do not compare
   allocator residue for modes 0/1/2.
4. Keep the allocation-end impulse behavior as a documented safety divergence.
   If a natural frame reaches it, retain the input and native deterministic
   output, but do not execute or bless the DLL's out-of-object load as required
   parity.
5. Continue to label positive fallback, preoccupied/multi-entry queue, and study
   actions 2/3/5 as control-only until a non-forced profile-9/type-12 lifecycle
   reaches them. Do not manufacture natural-frequency claims from direct calls.
6. Keep identify 151 in the operation ledger as a raw retry with nullable
   artifacts. Exclude its stale frozen Linux processed/probe fields from all
   extraction, anti-fake, matcher, queue, and study coverage counts.
7. Keep early validation retries distinct from the later stateful retry. The
   controlled `0x7531` witness returns quality/coverage `0/18`; runtime evidence
   and parity projections must preserve those metrics rather than applying the
   `0x29aa`/`0x29bb` zero-metric rule.

## Audit Verdict

The enlarged corpus is strong evidence for ordinary profile-9/type-12
authentication, score/outcome diversity, one/two-candidate gallery evaluation,
anti-fake output variation, negative fallback formation, and natural study
actions 0/1/4. Existing focused controls substantially extend semantic coverage
for late matcher policy, fallback publication, queue handling, decoder values,
and study actions 2/3/5.

It is not complete producer or state-machine coverage. The most important
unclosed evidence is clamp-before-admission on over-range input and the stateful
selector-2 extraction history path. Those gaps explain how strict parity can be
correct for every frozen operation while still missing relevant native behavior.
