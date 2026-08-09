# Persisted Order Admission Audit (2026-08-09)

## Scope And Authority

This focused read-only audit covers the profile-9/type-12 packed `a1` order
prefix admitted from persisted prints. Static and execution claims use
`GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.

The replacement strict corpus contains 478 runtime records and 450 explicitly
selected identify/verify operations across 17 sessions. Current-versus-DLL
validation passes all 450 exactly, including after-match, candidate, score,
queue, and lifecycle outputs. The defect below is therefore outside the natural
captured manifold and was established with canonical synthetic-valid mutations
of one real 40-feature gallery.

## Packed And Live Contract

`FUN_1800403b0` copies the declared `a1` bytes directly into live template
`+0x87e8`; it does not validate the defined prefix. The packer later copies the
same 200 bytes verbatim. For a profile-9/type-12 template with `N` features, the
first `N` dwords must be a permutation of `[0,N)`. The remaining 50-entry suffix
is reserved state and is not part of this admission rule.

`FUN_180055a40:0x180055d50` loads each traversal index with:

```text
MOVSXD RDI, dword ptr [R14 + RCX*4 + 0x87e8]
```

It then uses that value to select the live feature owner. The DLL assumes the
permutation invariant; neither decode nor match validates it.

## Exact Controls

Both controls changed only one defined order dword in a valid 40-feature packed
gallery and recomputed the ordinary packed CRC. All tags, lengths, features,
relations, metadata, and other tail bytes remained canonical.

1. Duplicate control: replace order slot 1 with order slot 0. The DLL accepts
   the template, returns identify score 25, and completes study action 4. The
   current implementation also admits it and returns score 25, although its
   action-0 publication model produces no candidate. This proves duplicate
   indices alter reachable feature traversal rather than being inert reserved
   bytes.
2. Out-of-range control: replace order slot 0 with 40. The DLL suffers an
   unhandled read page fault at address `0x150` before writing oracle output.
   Current persisted-print validation also accepts the template; matching then
   notices the out-of-range index and downgrades the gallery to invalid data.

## Root Cause And Boundary

`goodix_milan_print_validate_template()` validates packed structure, feature
record counts, relation topology, metadata, and byte-canonical repacking, but it
does not validate the defined order prefix. Canonical repacking cannot detect
the defect because the codec intentionally preserves `tail_state` verbatim.

The missing admission condition is atomic:

```text
for i in [0, feature_count):
    index = tail_state[i]
    require index < feature_count
    require index has not appeared earlier
```

This belongs at persisted-print validation before matcher or study traversal.
It should not be added as a DLL-compatible crash or silently repaired by sorting:
sorting malformed persisted state would accept and mutate an object whose
original traversal ownership is ambiguous. Existing study sort/finalization
validation is too late because ordinary matching consumes the order first.

## Negative Controls And Adjacent Bounds

The audit did not infer new bounds for opaque or arithmetic feature scalars.
In particular, changing physical feature 25 `b5` from 1 to 2 remains accepted
and produces byte-exact current/DLL score 25, action 4, after-match, and
after-study outputs. Although production writers currently use 0/1, that
control does not authorize a persisted-input rejection without a stronger
vendor-domain or safety contract.

Adjacent structural checks are already fail-closed:

- every feature has `1..150` records and `b7` partition count in
  `[0, record_count]`;
- `b6` row bases and registration count must equal the triangular feature
  layout;
- relation indices must be unique, in range, nonnegative in their validity
  word, and incident to the established reference;
- queue and graph flags are bounded before transient queue or graph consumers;
- persisted inputs and all production pack-call capacities are bounded to 1
  MiB, while codec additions and relation products have explicit overflow
  checks.

No second reachable defect was established in those adjacent contracts. The
order prefix differs because it is structurally preserved but not semantically
checked before its first pointer-owner consumer.

## Verdict

This is a high-confidence persisted-input admission defect with a DLL crash
surface. Natural current-versus-DLL parity remains exact because production
templates maintain the invariant. The minimal production correction is to
reject non-permutation order prefixes in
`goodix_milan_print_validate_template()`; no matcher, codec, relation, study,
score, or compatibility behavior needs to change.

## Resolution

Production commit `792f5b3` implements that admission check before any matcher
or study traversal. It reads exactly the first `feature_count` order dwords and
rejects an index when it is out of range or already present; the reserved suffix
remains untouched.

The release build succeeds, and the existing Milan synthetic, state, and runtime
suites pass. The canonical 40-feature control remains admitted with score 25
and study action 4, while duplicate and out-of-range controls reject during
persisted-print validation. Full campaign replay is not required for this
synthetic-only admission guard because every natural captured template already
satisfies the permutation invariant and the guard does not alter valid state.
