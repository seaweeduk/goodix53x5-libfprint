# Runtime Identify Arbitration

## Scope And Baseline

This contract covers profile 9 / sensor type 12 identify arbitration at
production commit `c182b140a86e70312bd9f637ba68cf8a9140f6c0`, from the ordered
gallery loop in `drivers/goodix53x5/milan/runtime.c` through study and final
candidate publication. Matcher, extraction, and study leaf policy are outside
this contract.

The native reference is `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
Its public candidate-array behavior is documented in
`functions/identifyImage.md`.

## Gallery Identity And Order

`goodix_milan_runtime_input_new()` copies every runtime gallery entry in caller
order. Each entry has two identities:

- `gallery_position` is the zero-based position in that copied runtime array;
- `gallery_index` is opaque caller metadata copied from the entry.

The loop never sorts, deduplicates, or interprets `gallery_index`. On a match,
`winner_position` is the runtime array position and `winner_index` is that row's
metadata. Duplicate indexes are accepted by the runtime API and can make
`winner_index` ambiguous without changing `winner_position` or selection.

The production authentication caller gives every admitted entry its original
libfprint gallery position as `gallery_index`. It may omit an `FpPrint` that
cannot be decoded into native template bytes, so the runtime array can be
compressed while the index still maps back to `task_data->originals`. Those
production indexes are unique. A direct runtime or parity caller can supply
duplicates, but normal authentication does not.

## Row Admission And Evaluation

A result row is appended when its position is reached, before validation. This
has the following observable states:

| State | `valid` | `evaluated` | score/hash/queue |
| --- | --- | --- | --- |
| template or encoded queue validation fails | false | false | score remains 0; no after-match hash or queue observations |
| matcher returns an error or no after-match bytes | false | true | score remains 0; pre/post-match queue observations exist; no after-match hash |
| matcher succeeds | true | true | exact score, acceptance, after-match hash, and queue observations exist |

Template validation failures increment `invalid_gallery_count` and continue.
Queue construction or validation failure changes an initially valid row to
invalid and continues. A matcher failure likewise changes the row to invalid,
decrements `valid_gallery_count`, increments `invalid_gallery_count`, and
continues. `evaluated_gallery_count` includes matcher failures because it is
incremented immediately after the matcher returns.

An invalid row does not overwrite the overall score. A successfully evaluated
row always writes its score to `output->score`, including a zero or negative
score.

## Winner And Overall Result

For each successfully evaluated row, `accepted` is exactly `score > 0`. The
runtime processes rows in input order and stops at the first accepted row. It
does not compare positive scores and does not emit result rows for later gallery
entries.

On the first positive row, the runtime publishes:

```text
status          = MATCH (0)
score           = that row's score
winner_index    = that row's gallery_index
winner_position = that row's gallery_position
match_result    = that row's complete matcher result
```

If no row is positive, the result is:

| Gallery outcome | status | overall score | winner |
| --- | --- | --- | --- |
| empty gallery | `NO_MATCH` (1) | 0 | `G_MAXUINT` / `G_MAXSIZE` |
| one or more valid evaluated rejects | `NO_MATCH` (1) | final successful row's score | sentinel index and position |
| nonempty gallery with no valid row remaining | `INVALID_DATA` (3) | 0 | sentinel index and position |

The third row has score 0 because no matcher call completed successfully. A
mixture containing an earlier successful reject retains that reject as a valid
row and therefore takes the `NO_MATCH` branch instead.

The DLL export has the same valid-candidate order rule. At
`identifyImage:0x180001e87`, signed `JG` selects the current candidate. Loop
exhaustion at `0x180001ea9..0x180001ec3` publishes the final matcher score and
`UINT32_MAX`; a positive result at `0x180001ec8..0x180001ed3` publishes the
current array position and returns immediately.

## Per-Gallery Mutation And Queue Ownership

Every admitted runtime row receives a new `GoodixStudyQueue` reconstructed from
that row's serialized queue state and transaction counter. The queue is not
shared across rows. Matching receives the common extracted probe, the current
input template, and only that row's queue.

Matching normalizes into new storage and returns a distinct `after_match`
`GBytes`; it does not modify the input `GBytes`. Match-time lifecycle updates are
included in those returned bytes. Queue enqueue occurs after after-match bytes
are formed, so queue occupancy is separate state rather than an implicit reading
of the after-match hash.

For an invalid row or a successful reject, the row queue is freed before the next
position. Reject after-match bytes remain observable through the debug result
and hash, then their local reference is released. Neither reject bytes nor its
queue feed a later candidate, study, or persistence.

For the first positive row, the runtime retains exactly two row-owned values:

- `winner_after_match`, the winner's returned after-match template;
- `winner_queue`, the same queue after winner matching.

The runtime then breaks. Later input templates are neither validated nor
matched. The DLL analog mutates each reached unpacked candidate in place,
retains only the first positive candidate at `0x18024e548`, and leaves later
candidates untouched.

## Study Handoff

Study receives the common probe, `winner_after_match`, the winner's
`match_result`, and `winner_queue`. It never receives the original winner bytes,
a rejected row's after-match bytes, or a queue reconstructed after arbitration.
This is equivalent to the DLL handoff: `templateStudy` operates on the exact
candidate retained by the positive `identifyImage` call.

The runtime sets `MATCH` before study. Study failure is therefore a learning
failure, not a recognition failure:

- a non-OK study status sets `learning_error` and retains `MATCH`, score, and
  winner;
- action 0 is a successful completed study and normally returns no candidate;
- action 0 with candidate bytes records `learning_error`, discards the bytes,
  and retains `MATCH`;
- actions 1 through 5 require nonnull after-study bytes, a valid native template,
  and bytes different from the original winner input;
- invalid action, absent/invalid positive bytes, or unchanged positive bytes
  records `learning_error`, publishes no final candidate, and retains `MATCH`.

The unchanged-byte check is against the original gallery input, not
`winner_after_match`. A valid action 1 through 5 publishes the after-study bytes
as `final_candidate`. Action 0 can still change transient lifecycle or queue
state inside the study transaction; no such transient state advances
persistence.

The DLL follows the same public lifecycle. `templateStudy` is called after a
positive identify even when it returns update 0. It writes the update code,
destroys the retained probe through `FUN_180037b10`, and returns status 0 for a
normal action-0 completion.

## Persistence Boundary

At the runtime boundary, persistence is eligible only when recognition matched
and study produced a validated, changed `final_candidate` for action 1 through
5. Action 0 and every learning failure leave `final_candidate` null.

The natural DLL runner expresses the same gate as
`matched && study_update > 0`: a positive update selects packed after-study
bytes as `next-persistent.bin`; otherwise it selects the original loaded bytes.
The parity record exposes a final candidate only for the advancing case.

Production authentication additionally requires the runtime output to remain
owned by the same action and generation, successful conversion of
`final_candidate` into print data, and successful scan-cycle completion. It
queues the update against the `FpPrint` selected by `winner_index` and applies
it only when both pending target and pending data survive to completion. A
`learning_error` is logged after the positive match but does not change the
match report.

## Cleanup

Each nonwinner queue is freed in the row that owns it. On every noncancelled
winner path, `winner_queue` is freed after study success or failure. The common
probe is freed on no-winner return and on every study return. Local
`winner_after_match` and `after_study` references use automatic cleanup unless
ownership is transferred to `output->final_candidate`.

`goodix_milan_runtime_output_free()` releases the final candidate, probe
template, gallery-result array, runtime error, and learning error. Each gallery
result releases its validation error and, in debug builds, its retained input
and after-match references. Input gallery templates remain owned by the runtime
input until `goodix_milan_runtime_input_free()` releases them.

## Native Parity Aggregation

The native parity backend invokes the DLL with one candidate per transaction.
`native-runner` expands an ordered gallery into those one-candidate jobs, runs
positions in order, and stops a case after the first accepted record. For valid,
evaluated rows, `combine_gallery_records()` reconstructs the production runtime
boundary as follows:

- preprocessing and probe phases must be byte-identical across row jobs;
- gallery rows retain input order and are assigned aggregate positions;
- the first accepted row truncates the aggregate exactly where runtime breaks;
- the final executed row supplies overall status, score, study action, lifecycle,
  and candidate;
- the first accepted aggregate position becomes `winner_position`;
- the winning row's caller index remains `winner_index`;
- if every row rejects, the final row's score/status remain overall and both
  winner fields are sentinels;
- each row retains its own after-match hash and natural queue observations;
- only the winner row has after-study queue observations.

This reconstruction is valid because production matching gives each row an
independent serialized template and queue, reject state is discarded, the probe
is unchanged between row calls, and study begins only after the first positive.
Fresh complete checker projections confirm the boundary below.

Invalid or unevaluated rows are outside this native aggregation boundary. Dump
validation marks any selected operation containing such a row structurally
unavailable before replay. `runner_projection()` therefore need not compare the
record's `valid` and `evaluated` flags: admissibility already requires every row
to be both valid and evaluated. Directly sending an invalid template to the DLL
oracle fails unpacking rather than producing a comparable native row.

## Contract Witnesses

All generation-use-1 witnesses below use no preprocessing prelude. The current
runner identity is source digest
`de4629d676889fbaa9bd4365b7ee1cf5672bb8d1bb1d03996625bf2f480653cf`
and backend SHA-256
`2d66a669c96d8764b2ca4a7216ddf55700b5be1628758c94537522429b5bad5d`.
For every native-admissible row, the complete checker projection has no field
difference, including processed/probe identity and counts, ordered gallery
outputs, overall result, winner, queue observations, candidate, and lifecycle.

### Reject Then Positive Action 4

```text
setup        d125efb9a807406a0e7ad8c7384ba88826ae7d20230e14cada6c7231a468b3a2
live         7e5157f4e955ebe12336d21137c236e1d76163184e2a531a89a8c81ce3d58184
gallery[0]   27fbc4a35d0bc18582d31c52a4e31804082dcb9a6d2c07a8ed4475d5ad7ab7bc
gallery[1]   9325710f80a48809ea5190b0a74d5287df57b7b009ef34852f084c2634d07399
rows         (-7, reject, after a73c26c99695c964c07f1408ee2a565f950460d5c4f8c43c6e37d36c14b1b12a, queue 0->1)
             (26, accept, after e9e9128cf01b0807f4bda127ecec23b9fa90382fc702613a19f60d7d68186e6b, queue 0->0->0)
overall      MATCH, score 26, index 1, position 1, action 4
candidate    0656dd56d3c06071f9bb5d8b2825d7fb0c167b398cf3724ea9c759c0df8ad461
lifecycle    preprocess/extraction/study attempted and completed
```

Replacing both caller indexes with `31337` preserves every byte, score, queue,
action, and lifecycle output. The winner becomes index `31337`, position `1`.

### Multiple Positives And Input Order

The same setup/live pair scores two gallery versions independently:

```text
setup           8f8a55aff83ccfb9b8c3c4b9b70847556eea85d7b9b34d01307b2655fcbcaa96
live            e8407a978f598e46cfd614838b43324e276181a6e5af500ff54fb54aa9279967
older gallery   d5ad334ed30ffcc36acba7b908cb6da74f7947b73728e8c016e0b04e9ad376ca -> 35
current gallery adb3fa07a57870b790bef6ea3e7aa06ddc29c63b70f5ae079f202be24e0c4f41 -> 26
```

Both single-candidate calls match with completed action-0 study. Ordered
`[older(90), current(91)]` returns score/index/position `35/90/0` and one row.
Ordered `[current(91), older(90)]` returns `26/91/0` and one row. The independently
higher score 35 is not evaluated in the second order. Current and native
projections are exact in both orders.

### All Negative With Differing Scores

```text
setup        b8bab814600d60de58134e806c4baac31e17a3860c12c39f1bbb9a3933d1b48c
live         ef4539ff64078fb1b59f2a59a50b7d38829ec10a4c486e1a38ebff5588036226
rows         -4 then -1
after-match  f5dedeb706f39e8d2328e5128006aa7c405c2b7b1e48772de7c1119493b9280c
             2a7ee9ed1e85a42d1b3513507900d77fedc616f319befde771c4e99ed16ef9c9
overall      NO_MATCH, score -1, sentinel winner, no study or candidate
```

Both rows have queue occupancy `0->0`. Preprocessing and extraction are
attempted/completed; study is neither attempted nor completed. The complete
current/native projection is exact.

### Action 0

The current gallery from the multiple-positive witness returns score 26,
`MATCH`, winner `0/0`, completed study action 0, after-match SHA-256
`795074d91a45be6702dd39b58f1ca956fb7b2e23b5bb0e0a9c32eed7c0800e94`,
queue occupancy `0->0->0`, and no candidate. The complete current/native
projection is exact.

### Invalid Rows

With the action-4 valid rows around an invalid 19,008-byte frame, the current
runtime emits positions `0,1,2`: reject `-7`, invalid/unevaluated score 0 with
null hash and queues, then accepted `26`. Overall output is `MATCH`, index 7,
position 2, action 4, and the same action-4 candidate. With two invalid frames,
overall output is `INVALID_DATA`, score 0, sentinel winner, no study, and two
rows with null hashes and queues.

Both cases are intentionally native-unavailable. The one-candidate DLL oracle
fails at the invalid row (`gallery_index_u32=6` or `8`), matching the dump
validator's rule that invalid or unevaluated rows cannot enter native replay.
