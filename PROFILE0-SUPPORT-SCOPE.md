# Profile 0 Support Scope

Research handoff for contributors considering profile-0/type-0 support.
Maintained on `milan-dev` with the reverse-engineering notes, not on `main`.

## Decision And Evidence Boundary

Profile 0 is not supported by the current driver. Maintainer-led investigation
is paused because we do not have the hardware. These findings and remaining
tasks are available for a contributor with a matching device who wants to
investigate further; there is no implementation commitment or timeline.

This document is a research map, not an installation guide or a patch that
enables the device. Do not bypass compatibility gates or replace a PSK or
firmware merely to follow this handoff. Persistent device changes can affect
Windows compatibility.

Native authority: Windows driver DLL pair version 2.0.310.900. The comparison
covers profile 0/type 0 against profile 9/type 12. Production source was
inspected at commit `0608d723`; current-code paths below are relative to
`drivers/goodix53x5/` unless otherwise stated. Native addresses and behavior
must not be assumed to apply to a different DLL version.

The [reference-driver download and DLL identities](re/milan/REFERENCE-DRIVER.md)
identify the exact Dell package used for the research. Obtain the reference
from Dell; do not publish vendor binaries or private capture artifacts here.

The relevant native orchestration and subtype-dependent branch inventory is
substantially mapped across both DLLs, including the follow-up metadata,
sensor identity, histogram producer, and capture-payload consumers. This is not
an exhaustive recovery of every numeric kernel, native/current byte parity,
or hardware validation. A fixed schedule is not established.

## Durable Native Owners

- [USB contract](re/milan/PROFILE0-USB-CONTRACT.md): communication, calibration, references,
  events, lifecycle, sensor checking, capture ABI, and OTP-derived identity.
- [Engine contract](re/milan/PROFILE0-ENGINE-CONTRACT.md): preprocessing, coating, calibration
  persistence, gain lifetime, image metadata, and quality.
- [Match/template contract](re/milan/PROFILE0-MATCH-TEMPLATE-CONTRACT.md): extraction, match policy,
  enrollment, study, graph operations, anti-fake data, codec, and admission.
- [Chip mapping](re/milan/CHIP-ID-PROFILE-MAPPING.md): chip family/profile/subtype distinction.

Native facts belong there; implementation proposals and remaining validation
work belong here. In particular, diagnostic +0x4fb4 has an explicit histogram
producer, and type-0 c7 is not uniformly zero. The owning notes explain both
contracts; neither allocator state nor a zero auxiliary buffer is a substitute
for tracing the actual inputs.

## Implementation Work Packages

| Package | Current Owners | Required Work | Reuse / Preservation Boundary |
| --- | --- | --- | --- |
| Profile selection | device/base.[ch], device/session.c, driver-private.h, milan/runtime.[ch] | Select a validated descriptor once from chip family; keep USB profile and algorithm subtype separate; propagate identity through runtime snapshots | Retain fail-closed selection and unchanged profile-9 behavior before adding type 0 |
| USB calibration/config | device/calibration.[ch], device/commands.[ch] | Profile-0 OTP integrity/calibration, register-derived fields, config template/patches, FDT base encoding, image payload and event interpretation | Share transport/framing/decoder mechanisms, not profile-9 command semantics |
| Reference/capture lifecycle | device/base.[ch], device/scan.[ch], device/session.c | Separate temporal-stability acquisition, 14-row auxiliary sampling, image classifier, stateful validity, up/reverse/down transitions and timers | Preserve shared request ownership and libfprint cancellation guarantees; do not clone the entire driver |
| Preprocessing state | milan/preprocess/state.h, device/base.h, milan/runtime.c, device/persistence.c | Keep gains, auxiliary count, temporal state, calibration scalars and ready flags device/generation-owned; explicit cold/reset/reload boundaries | Current state is already generation-owned; extend it rather than reproduce DLL globals |
| Preprocessing stages | milan/preprocess/, milan/core.c and related current numeric owners | Type-0 baseline/source arithmetic, gain narrowing, temporal limits, foreground mask, row-oriented refinement, coating and distinct quality implementation | Parameters for true constant differences; separate stages where formulas/ownership differ |
| Extraction/anti-fake | milan/feature/, milan/antifake/ | Record budget, second-pass policy, validity pruning, residual borders, 104-column packed masks and shifted X coordinates | Share geometry/descriptor/record machinery where the native contract agrees |
| Matching | milan/match/ | Capacity/denominator 31 vs 42, thresholds, affine selection, classifier/veto/fallback policies and metadata reachability | Share fitting/scan/graph primitives; keep policy distinctions explicit |
| Enrollment/study | device/enroll.c, milan/enrollment/, milan/study/, milan/match/study.c | Confirm chosen enrollment policy; type-0 tie ordering and subtype-aware queued rematching | Shared ordinary enrollment and queue controllers; do not import reporter's eight-stage setup as this DLL's default |
| Print/cache compatibility | milan/print.[ch], milan/template/, device/persistence.c | Explicit validated profile/subtype at envelope and inner-template boundaries; retain existing profile-9 prints | Same native record layout is not cross-type compatibility; no speculative migration |
| Debug/parity | device/debug.c, tools/milan-parity/ native/current runners, policy/build/validate/replay modules | Explicit profile-specific evidence and native policy; preserve exact current schemas and build seals unless separately approved changes require otherwise | No accepting type-0 evidence under a type-12 fixed-policy label; no relaxed legacy fallback |
| Release/hardware gate | device identification/support gate and existing build/UAT workflow | Enable only after native/current comparisons and physical lifecycle validation | Detector recognition stays separate from support |

## Structural Constraints

Use a small immutable descriptor for verified identity, geometry, constants,
device-family operations and algorithm policy. Do not add one switch per
numeric helper, a universal callback for every function, or a directory-sized
copy of the driver. A few distinct family-level sequences are preferable to
one branch-heavy universal state machine.

Both current candidates have 88x108 raw and 88x104 matcher geometry. Do not
perform a repository-wide variable-dimension rewrite for hypothetical future
profiles. Record geometry in the descriptor and maintain explicit validated
bounds; generalize allocation/storage only where the supported profiles need
it. Current preprocess/runtime structs have fixed arrays and ABI assertions,
so state-layout edits also affect parity/tooling contracts.

The existing transport is not wholly family-neutral: device/transport.c carries
GoodixProfile9FdtWaitMode cancellation/drain state. Audit this integration when
separating scan policy rather than assuming all transport code stays untouched.

Current calibration persistence already hashes chip ID, subtype, OTP length
and full OTP and checks stored subtype/dimensions. Preserve this stronger local
identity rather than weakening it to Windows' first-sixteen-OTP-byte equality.
Current print.h fixes profile 9 and subtype 12; validation/build/parse APIs need
an explicit expected profile policy when another subtype becomes supported.

## Validation And Open Decisions

- Native execution feasibility for profile 0 has not been demonstrated. Current
  parity policy, wrappers, oracle output and validators are explicitly fixed to
  profile 9/type 12. Inspect initialization/isolation before attempting profile-0
  runs; a profile-index substitution is not a sufficient validation plan.
- Native process globals can retain the preceding coating scalars across profile
  selection/default setup. Use isolated native processes or a proven reset
  boundary for comparison; do not assume ppp_param_init is a cold reset.
- Preserve the current strict dump v3, print schema 4 and build seal v2 workflow.
  Any deliberate format evolution needs separate approval and compatibility
  reasoning; extending supported policy is not permission to accept old schemas.
- Compare processed image and status/quality/coverage, complete preprocessing
  state, probe, gallery-after-match, candidate, queue/order/counts and lifecycle
  outputs. Captured queue evidence must never become injected runner state.
- Use existing profile-9 suites, strict native parity and hardware UAT as the
  preservation gate for structural changes. Obtain maintainer approval before
  adding test cases/files. No tests or native execution were performed in this
  scoping pass.
- Profile-0 hardware remains needed for cold start, event delivery, reference
  recovery, retries, cancellation/reopen, enrollment/verify and suspend/resume.
  No hardware-support claim follows from this static map.
- The issue's two-byte image request and this DLL's four-byte request remain
  distinct observations. Resolve with authorized protocol evidence before
  choosing a wire compatibility policy; no speculative fallback.
- Factory credential availability remains outside this investigation. No key
  recovery, persistent PSK change, or firmware operation is authorized.
- Native cancellation/cleanup imperfections are not requirements to reproduce
  leaks or non-cancellable shutdown in libfprint. Specify bounded cancellation
  and safe ownership explicitly, with native success-path semantics preserved.
- The surplus native sample half is copied by the adapter but not submitted to
  ordinary live preprocessing. Do not reproduce unspecified padding as an
  algorithm input. Broader sample-export consumers and sensor-info +0x24 are
  outside the required algorithm boundary; revisit only if implementation uses
  them.

## Suggested Sequence

1. A contributor with the hardware can review this scope and discuss a bounded
   implementation proposal and required validation cases with the maintainer.
2. Establish profile-0 oracle policy and isolated-state execution feasibility.
3. Introduce the minimal selection/ownership boundary with profile 9 as the only
   enabled implementation; verify unchanged profile-9 behavior independently.
4. Implement device calibration/capture and engine differences as separately
   reviewable packages, keeping hardware support disabled.
5. Complete print/cache and debug/parity policy integration; verify profile-9
   persisted prints and calibration behavior remain compatible.
6. Complete profile-0 native comparisons and physical UAT, then independently
   decide whether to enable the chip family.

Keep experimental production changes separate from RE documentation, and keep
this handoff and `re/milan/` out of `main` and production PRs. Share protocol
observations without publishing fingerprint images, templates, credentials,
or other private device artifacts.
