# Runtime Identify Arbitration

## Scope

This contract covers profile 9 / sensor type 12 identify arbitration, from the ordered
gallery loop in `drivers/goodix53x5/milan/runtime.c` through study and final
candidate publication. Matcher, extraction, and study leaf policy are outside
this contract.

The native reference is `GoodixEngineAdapter.dll` 2.0.310.900.
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

The decomposition has the following ownership preconditions:

- Each DLL candidate is a separately unpacked internal template. The export
  borrows handles and permits aliases; repeated slots that alias an internal
  template can carry earlier mutations into a later row. Separately unpacked
  copies of equal bytes, and duplicate opaque caller indexes, do not alias.
- Each row begins with its own natural queue, derived from its input template.
  A rejected row may enqueue even though no study follows. Its queue is not an
  input to the next row or the winning study.
- Every job reconstructs the same complete live probe, including live records,
  classification summary/projection, packed class, masks and anti-fake owner.
  Equal packed probe bytes alone do not establish this precondition: live record
  fields and classification `+0x158` are not all serialized.
- The first candidate's extraction configuration must be equivalent in every
  job, and preprocessing/extraction history must start from the same state.
  Ordinary type-12 configuration is `[12,104,88,1,1,150,150]`; enrolled feature
  count is not an extraction configuration field.
- Matching shares the probe read-only. Its per-row class accumulation and
  evidence reset do not feed another row. Study runs only after the first
  positive and receives that exact after-match gallery, evidence and queue.

Under these preconditions, ordered one-candidate decomposition and the DLL's
actual candidate-array loop have the same reached rows and winner handoff.
This is a boundary contract, not a claim that matching serialized probe
projections verifies all of the reconstruction preconditions.

This read-only lifetime includes producer-generated live modes 3, 4, and 5,
not only the three-byte summaries. `FUN_180071d40` receives the exact shared
`+0x158` pointer in its live-classification context. Its masked-class arm calls
`FUN_180072710` with separately allocated temporary planes; it does not rewrite
that owner, the preprocessing auxiliary source, or retained extraction history.
A rejecting row that reaches this arm can therefore be followed by a positive
row using a different gallery class or geometry without advancing or replacing
the probe classification. The shared owner remains intact at the winner study
handoff. A positive score still does not imply a study update: an action-0
completion consumes the probe but retains the original persistent gallery.

Mode 4 is a reachable selector-zero producer state: primary seed 1 plus more
than 600 stable class-1 pixels yields the same history-projection ownership as
mode 3. Mode 5 instead samples the same-call auxiliary decision plane. Equal
packed `c7` values neither distinguish these producers nor establish equality
of their live projections. See `functions/identifyImage.md` and
`functions/FUN_180048260.md`.

Invalid or unevaluated rows are outside this native aggregation boundary. Dump
validation marks any selected operation containing such a row structurally
unavailable before replay. `runner_projection()` therefore need not compare the
record's `valid` and `evaluated` flags: admissibility already requires every row
to be both valid and evaluated. Directly sending an invalid template to the DLL
oracle fails unpacking rather than producing a comparable native row.

## Native Ownership References

- `functions/identifyImage.md`: borrowed candidate array, first-positive return,
  public array position, retained winner and probe lifetime.
- `functions/FUN_18005edb0.md`: per-candidate policy and blocker-seed reset.
- `functions/FUN_180055a40.md`: read-only probe consumers, row-local workspaces,
  gallery lifecycle and natural queue producer.
- `functions/FUN_180048260.md`: live classification summary/projection and its
  distinction from packed `c7`.
- `functions/templateStudy.md`: retained winner dispatch and probe destruction.
