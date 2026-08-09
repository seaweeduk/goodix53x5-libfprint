# Runtime Publication And Cancellation Audit (2026-08-09)

## Scope

This audit covers profile-9 authentication candidate ownership after native
study returns, through libfprint result publication. It distinguishes runtime
worker cancellation from driver/scan-SSM cancellation and does not infer any
behavior from another sensor profile.

The replacement strict corpus has no cancellation rows, but all 450 selected
ordinary identify/verify operations pass current-versus-DLL exactly. It includes
48 action-0 matches, all with study attempted/completed, no final candidate, and
exact queue state. Existing runtime tests also pass, including cancellation
while study is blocked and stale task/action/generation ownership controls.

## Correct Worker Boundary

`goodix_milan_runtime_run()` checks cancellation after extraction, around every
gallery, before study, and after study. The final check is
`runtime.c:559..566`; `output->final_candidate` is not assigned until
`runtime.c:603`. `goodix_milan_runtime_cancelled()` also clears any candidate
and invalidates preprocess-state publication. Cancellation observed by the
worker therefore cannot publish learned bytes.

Action 0 is also correct at this boundary. Study completes and may mutate
transient/after-match lifecycle state, but action 0 returns no serializable
candidate. The driver consequently reports a successful match with
`updated=false`; no packed gallery mutation is published.

## Late Driver Cancellation Defect

`goodix_auth_task_done()` takes one cancellation/ownership snapshot at
`auth.c:242..255` and discards an already-cancelled output at `282..290`. For a
successful, currently owned result it then calls `goodix_auth_build_update()`
at `auth.c:326`. That helper immediately calls `fpi_print_set_raw_data()` on the
caller-owned gallery print at `auth.c:204`.

The private libfprint update-result patch does not make that early mutation a
commit point. `fpi_device_*_complete_with_update()` stores `updated` only in the
successful, already-reported match path, and both finish APIs expose it only
when terminal completion succeeds. The fprintd patch saves the mutated print
only when `success && match && updated`. A cancelled completion therefore
correctly suppresses persistence/reporting, but it cannot restore the in-memory
object changed earlier by the driver.

The authentication result is not public or complete at that point. The task
callback only queues a pending report and sets scan disposition. Authentication
success does not wait for finger-up: `goodix_scan_disposition_waits_for_up()`
excludes `GOODIX_SCAN_DISPOSITION_AUTH_SUCCESS`. Instead,
`goodix_scan_set_disposition()` starts asynchronous stop/receive cleanup at
`scan.c:831..850`. The report is flushed and the action completed only after
that cleanup returns through `goodix_verify_ssm_done()` at `auth.c:570..594`.

This creates a reachable cancellation window:

1. Runtime study succeeds and returns a final candidate.
2. `goodix_auth_task_done()` mutates the original `FpPrint` and queues the
   pending success report.
3. Before scan stop/receive cleanup settles the SSM, the user cancels.
   `goodix_cancel()` increments the action epoch and cancels the driver
   cancellable.
4. The scan cancellation callback adds `G_IO_ERROR_CANCELLED` to the already
   requested stop, and the scan SSM completes with that error.
5. `goodix_verify_ssm_done()` clears the pending report and returns the error,
   but `goodix_clear_pending_result_report()` has no old template bytes and
   cannot undo the already mutated print.

The caller can therefore receive cancellation with no update publication while
its print object has learned bytes. The window spans asynchronous scan
stop/receive cleanup rather than finger release.

## Minimal Ownership Correction

Do not mutate the gallery print in `goodix_auth_task_done()`. Queue an owned
final-candidate reference (or validated raw-data variant) together with its
target print. Apply it only in the no-error final SSM path immediately before
flushing the success report and calling the `*_complete_with_update()` API.
Cancellation, stale ownership, close, and any scan/SSM error must clear the
pending candidate without applying it.

This correction changes only publication timing and ownership. Runtime study,
candidate bytes, action 0, scores, queue state, gallery selection, and the
reported `updated` value on successful operations remain unchanged.

## Existing Coverage Gap

The existing cancellation test cancels before capture and while study is
blocked, so the runtime/task callback observes cancellation before mutation.
The stale-result tests change ownership while study is blocked. Neither test
cancels after the task callback has queued success but before scan-cycle
settlement. No new test was added during this read-only audit.

## Verdict

Worker-level cancellation and action-0 ownership are correct. Authentication
candidate publication is too early and can mutate a persisted print on an
eventually cancelled action. The minimal fix is deferred candidate ownership at
the final successful SSM boundary; no matcher or DLL behavior needs to change.

Production commit `3b3a610` implements that boundary. Candidate data and target
remain pending until successful `goodix_verify_ssm_done()` immediately before
the existing report flush; cancellation/error clears them. Authentication keeps
the same no-wait-for-up disposition and public completion timing. The focused
late-cancellation test passes in 0.16 seconds and all Milan suites pass under the
60-second cap.
