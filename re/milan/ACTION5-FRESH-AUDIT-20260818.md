# Profile-9/Type-12 Action-5 Fresh Audit (2026-08-18)

## Scope And Authority

This audit rechecks the complete exported action-5 path against current
production source at `87a02ffb50c5dd9dabd3ad4bf4cbd72f7ad6b46b` and
`GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
It is limited to profile 9 / sensor type 12. Historical datasets and notes are
supporting evidence; fresh Ghidra disassembly is the static authority.

## Verdict

No action-5-specific queue, handoff, finalization, publication, or cleanup defect
is confirmed. Current source matches the DLL for producer admission, queue
ownership, physical traversal, remove/rerank, continuation, selector timing,
action override, capacity shutdown, final ordering/tail generation, candidate
publication, and cancellation.

One inherited synthetic-valid defect could affect a final action-5 candidate:
the pre-fix shared ordinary append implementation materialized an
established-graph reference edge for matched `b5 != 0`, while the DLL requires
exact `b5 == 1` at
`FUN_1800469f0:0x180046c37..0x180046c4c`. The packed validator accepts `b5=2`.
This divergence applies when either the primary or a queued inner selector
appends; it belongs to the generic append contract, not `FUN_18005d330`. Known
producers remain Boolean, so natural action-5 impact is not established. The
shared append predicate is now corrected to exact one. Its aligned direct
current/DLL witness is byte-identical, and the existing suites plus both standing
campaigns pass at `450/450` and `643/643`.

Invalid queue structures and allocation/packing failures are handled more safely
by current source; the DLL assumes valid live owners and has no transactional
rollback.

Natural frequency remains unestablished. Neither current-schema standing set
contains a preoccupied queue, a match-time enqueue, or action 5. The older V5
capture's 13/14 Linux action-5 rows are explicitly enqueued consumer controls,
not natural DLL producer observations. A later no-injection DLL replay returned
occupancy zero and action 1 for those rows.

## Export And Primary Boundary

`templateStudy:0x180001fe3..0x180002006` calls `FUN_180044fc0` only when the
matched gallery exists and the retained export gate is nonzero. Gate zero writes
action 0, leaves gallery queue/order/tail state untouched, and still destroys the
single-use retained probe at `0x18000206f..0x180002076`. Current
`goodix_match_study_feature_internal()` returns before queue or finalization work
at `match/study.c:169..179`, matching that export boundary.

For a dispatcher-entered type-12 primary action,
`FUN_180044fc0:0x180045078..0x1800450aa` requires nonzero action evidence,
enabled mode, quality `>15`, and coverage `>65`. A nonnegative selected index and
queue state zero call `FUN_18005d330` at `0x1800450af..0x1800450e2`. Follow-up
status 5 alone overrides the primary action at `0x1800450e7..0x1800450f7`.
Current `goodix_match_study_feature_queued()` implements the same sequence at
`match/study.c:597..665`.

## Producer Contract

The match-time producer is
`FUN_180055a40:0x180056cee..0x180056d4b`. Every condition is conjunctive and
strict:

1. current rejection evidence `+0x688 == 0`;
2. matcher configuration word 13 equals 1;
3. decoded probe low class equals 0;
4. probe coverage is `>65`;
5. probe quality is `>15`;
6. accumulated decoded high class is `<4`;
7. the live probe's primary auxiliary/histogram class is `<3`;
8. template queue state `+0x8e00 == 0`.

The aggregate-rescue call at `0x180056c75..0x180056caf` precedes this producer.
At `0x180056cb4..0x180056cc1`, the matcher then clears `+0x688` when stack local
`[rbp-0x50]` is zero. The producer precedes normal aggregate publication and
fallback and consumes rejection evidence after that clear. Strong rescue
suppresses the producer only when its rejection evidence survives. Current
`milan_match_prepared_probe()` computes the predicate before rescue at
`match/match.c:1560..1608`, and `match/lifecycle.c:155..167` deep-enqueues after
the final result. The exact distinguishing conjunction is documented in
`re/milan/MATCH-CONTRIBUTION-CONTRACT.md`.

The separate late producer is
`FUN_180044fc0:0x180045150..0x18004519b`. It requires enabled mode, no primary
selected index, action evidence exactly 1, queue state zero, coverage `>65`, and
quality `>15`. It can leave an action-0 diagnostic queue entry but cannot invoke
follow-up or emit 5 in that transaction. Current source mirrors this at
`match/study.c:603..627`.

`FUN_1800462a0:0x1800462e1..0x180046409` uses ranks as contiguous insertion ages.
It compares only the newest entry and suppresses an adjacent duplicate when the
identity metric is strictly `>190`. An empty physical slot receives `max+1`; a
full queue overwrites rank zero, decrements all occupied ranks, and assigns rank
19. `FUN_180045a50` deep-copies the live feature, transforms each type-12 record,
and clears transient live fields rather than borrowing the probe. Current
`study/queue.c:120..175` and `match/info.c:652..689` match this valid-state
contract.

## Consumer And Continuation Contract

`FUN_18005d330:0x18005d413..0x18005d57d` seeds a 20-dword modulo ring with the
primary selected index. For each ring trigger it scans occupied physical queue
slots `0..19`; ranks control age, not traversal. Queued calls set configuration
words 13/14 to `0/triggering_index`, re-match the deep-owned transformed probe
against the same primary-mutated, unfinalized gallery, and call
`FUN_1800469f0` only when queued acceptance evidence is nonzero.

A selector return `>=0` consumes the physical entry, decrements every greater
rank, appends the returned gallery index to the ring, and reports status 5. If
the returned index equals the current trigger, the helper stops only the current
physical scan; the remaining ring state controls termination. Rejection,
positive fallback, aggregate selected `-1`, and selector return `-1` retain the
entry. Matcher-owned gallery lifecycle writes made before a selector no-update
are not rolled back.

Current `goodix_study_queue_process()` at `study/queue.c:178..233` implements the
same physical/ring behavior. `goodix_match_study_followup()` at
`match/study.c:426..495` preserves matcher changes in the shared gallery, commits
and installs a live owner only for a positive queued study action, and signals
consumption only with a nonnegative selected index.

## Finalization, Publication, And Cleanup

The dispatcher disables and deep-frees the queue when feature count reaches
capacity at `0x1800450fb..0x180045149`. It then runs the accepted-evidence
type-12 finalizer exactly once at `0x1800451a0..0x1800451c6`, after all queued
mutations. Action identity does not gate this finalizer. Current source performs
capacity shutdown at `match/study.c:653..654` and one final transaction pack at
`match/study.c:656..665`.

The adapter publication boundary is `FUN_18002bbf0`: only update `>0` reaches
`templatePack`, and action 5 is returned unchanged. Update 0 is never packed.
Current runtime validates a positive candidate before assigning
`final_candidate` at `runtime.c:568..603`; cancellation is checked after study
and before assignment at `runtime.c:559..566`. Authentication retains the
candidate data and target until successful scan-cycle settlement, so late
cancellation clears them without mutating the persisted print.

Queue bodies and ranks are never serialized. Only `fa/fb` state and transaction
counter are packed, and adapter cleanup destroys all live candidate handles.
Current runtime likewise owns one queue per evaluated gallery and frees it on
every match, no-match, error, cancellation, and publication exit.

## Evidence Classification

- Natural current-schema evidence: the 450-operation set has actions
  `null/0/1/4 = 56/48/56/290`; the 643-operation set has
  `null/0/1/2/4 = 51/89/56/5/442`. Both have zero queue occupancy before and
  after match and zero action 5. Five action-0 operations retain one late queue
  entry after study across the two sets; those entries are discarded and not
  published.
- Controlled valid evidence: one-entry and two-entry DLL controls prove physical
  slots `0` then `5`, count transitions `9->11` and `9->12`, removal/reranking,
  continuation, external action 5, and exact adapter packing. Sequence 3 proves
  live Q/unfinalized G handoff and final SHA-256
  `c971239885adccdcaece80b31dc9e5e3abea6b0cc428b1b82307cb05fe12b181`.
- Synthetic-valid evidence: full/state-zero actions 2/3/4 can generically be
  superseded by 5, although canonically evolved full galleries persist state
  one. Gate-zero with a full allocated queue leaves it untouched in both DLL and
  current source; the suspected divergence is refuted. Established-graph
  `b5=2` append is the one confirmed inherited defect: current source creates an
  appended reference edge that the DLL omits, including when that append is the
  primary or queued inner mutation of an externally reported action 5.
- Malformed/unsafe evidence: noncontiguous ranks, null queue owners, invalid
  order indices, and invalid packed templates are rejected by current source.
  DLL helpers assume those invariants and may assign malformed ranks, retain a
  rank after copy failure, or fault. No parity correction should reproduce those
  unsafe failures.
- Resource failure: current allocation, matcher, study, final-pack, and
  validation failures discard the candidate and report a learning error. DLL
  mutation helpers have no rollback and `_CheckUpdateTemplate_E` ignores
  `templatePack` status. This is an intentional safety difference, not a
  successful-input action-5 defect.

## Validation

The existing `goodix53x5-milan-synthetic`, `goodix53x5-milan-state`, and
`goodix53x5-milan-runtime` suites pass `3/3` on current source. No standing
campaign was rerun because production was not edited, the inherited append
finding is synthetic-only under known Boolean producers, and both current sets
contain zero action-5 operations.
