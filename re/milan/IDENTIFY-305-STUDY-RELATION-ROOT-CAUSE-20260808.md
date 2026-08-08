# Identify 305 Study Relation Root Cause (2026-08-08)

## Scope

This note is limited to profile 9 / live sensor type 12 and frozen selector
`da0f8d98-6e9e-4cee-99d5-f7109e402baf/identify/305`. The operation follows
retry identify 304, whose preprocessing status is `0x29aa`. Gallery position 1,
public gallery index 1, wins. The approved DLL returns study action 4.

Input case:

`/home/anthony/.local/state/milan-parity/work/dump-da0f8d986e9e4cee99d5f7109e402baf-identify-305-c513ebb2c287`

The current and approved-DLL paths are exact through processed image, serialized
probe, score, selected gallery, and after-match template:

- processed SHA-256: `54b15a34f71d654cb5020b360b709f867f4fe92f1adf6f4a89db1af183c11a61`
- probe SHA-256: `0ef0621f5fe105f195c9b9cb3c43c8452b3023831ff0f290a086ecd173d14c04`
- after-match SHA-256: `73ea08e33ce7fa11e5cc9160951bf0b7549ebe434f799a329ecc891cbf798c31`
- score: 26
- selected live feature after rescue: 8
- study action: 4

The approved after-study SHA-256 is
`e1094cc9a9a08af3185bb8f41d4c86300eefd831cf9652600e4e84e6e1208f0a`.
Current produces
`c3291376c0339ac5b1c6e958f2c4af3ef5ec0d4e349704ff44d62cb094cd3fbc`.
The 487055-byte candidates differ only in outer CRC bytes 1 through 4 and the
six affine words of packed relation index 742 at offsets 485668 onward:

```text
approved: [254,-35,5792,35,254,-2496]
current:  [256,-16,3348,16,256,-2342]
```

## Independent Match Owners

Three direct contributors survive the current diagnostic reconstruction:

```text
feature  metric[1]  final flags  direct affine
8        11         0/0          [268,74,-16487,-74,246,18204]
26       5          0/0          [255,8,-11840,1,255,-7528]
33       10         0/1          [269,-46,19097,73,241,-1619]
```

All three are active (`b5=1`), contribute to aggregate state, and are not
anti-fake blocked. Feature 33 is the only direct-positive (`flag64`) producer.
At `FUN_180055a40:0x180056eec..0x180056f58`, the DLL independently compares an
active direct-positive candidate's metric word 1 against evidence `+0x668`.
Only strict greater updates the count and calls `FUN_1800619a0` at
`0x180056f28`; ties retain the earlier relation producer. This owner is
independent of the final selected feature and score.

The gallery reference is feature 0. Feature 33 is above the reference, so
`FUN_1800619a0:0x1800619c9..0x1800619f1` reads canonical relation slot 529,
whose stored feature-33-to-reference transform is:

```text
[256,22,-13165,-22,256,765]
```

It calls `FUN_180068860(stored, direct, output)`. With direct feature-33 affine
`[269,-46,19097,73,241,-1619]`, the exact result is:

```text
compose([256,22,-13165,-22,256,765],
        [269,-46,19097,73,241,-1619])
  = [254,-35,5792,35,254,-2496]
```

`FUN_180068860:0x180068899..0x18006897f` performs signed Q8 composition with
64-bit wrapping products/additions, arithmetic right shift by 8, signed 32-bit
narrowing, and wrapping translation addition. It then calls
`FUN_1800687c0`, which normalizes the four linear words to the Q8
rotation-only form while preserving translations. There is no inversion in
this case because feature 33 is greater than reference 0.

## Rescue Does Not Own Relation Evidence

The post-loop aggregate rescue is called at
`FUN_180055a40:0x180056caf`. On this case it succeeds with selected feature 8,
best count 11, score 26, three large marginal increments, and a prior direct
score of 20. The strong branch therefore copies feature 8's direct affine
`[268,74,-16487,-74,246,18204]` to selected transform evidence `+0x650`.

That selected affine routes through feature 8's slot 29
`[241,-88,25127,88,241,-13811]` to:

```text
[256,-16,3348,16,256,-2342]
```

This is exactly the wrong current after-study relation, which identifies the
current overwrite source.

Ghidra fixes the native ownership boundary. `FUN_18005d9e0` writes only:

- strong rescue: `+0x688`, `+0x648`, and `+0x650..+0x660` at
  `0x18005dd4b..0x18005dd7f`;
- every successful rescue: `+0x684`, `+0x648`, and score at
  `0x18005dd88..0x18005dda3`.

It has no read or write of relation count `+0x668` or relation affine
`+0x66c..+0x680`. Rescue therefore changes selected feature, selected affine,
score, and action gates while preserving the earlier active direct-positive
relation winner. Selected transform and study relation transform are
intentionally different after this mixed direct-then-rescue path.

Current `milan_match_prepared_probe()` instead enters its `rescue_applied`
branch, resets `relation_count` to zero, and calls
`goodix_milan_match_reference_transform()` using rescue-selected feature 8 and
the rescue-selected affine. The local feature-33
`GoodixMilanActiveRelationWinner` is valid at that point (`feature=33`,
`count=10`) but is discarded. This is the root cause.

## Study Replacement Path

`FUN_180044fc0` passes the unchanged evidence block to `FUN_1800469f0`.
Action-4 assembly `0x180046dd5..0x180046e15` copies matched lifecycle state,
calls `FUN_1800458c0` to replace selected study target feature 39, then calls
`FUN_180045d40(template, 39, evidence+0x66c)`.

The live reference is feature 0. Because target 39 is greater than reference 0,
`FUN_180045d40:0x180045f1d..0x180045f42` writes the supplied transform directly,
without inversion, to canonical target/reference slot:

```text
feature39.b6 + reference = 742 + 0 = 742
```

The packed relation is ordinal 37 of 38 and has relation index 742. The target
selection, feature copy, lifecycle scalars, relation cardinality, and every
other packed byte are exact. Only the transform argument reaching this helper
is wrong in current source.

`FUN_180047120`, called at `FUN_1800469f0:0x180046e8c` for action 4, reads the
reference star to recompute feature `bb` and overlap counts. It does not mutate
relation records. It cannot create or repair this mismatch. Packing only
serializes the already-installed slot and recomputes the outer CRC.

## Minimal Generic Correction

The exact correction belongs to the `rescue_applied` publication branch in
`drivers/goodix53x5/milan/match/match.c`, not study replacement, relation
composition, normalization, retry handling, or packing:

```text
publish rescue-selected score/index/selected affine as today
publish the accumulated direct lifecycle mask as today
if active_relation_winner.valid:
    relation_count = active_relation_winner.count
    relation_values[0] = 0
    relation_values[1..6] = active_relation_winner.routed
else:
    preserve initialized relation count zero and identity relation affine
```

In particular, do not recompute relation evidence from the rescue-selected
feature. The DLL rescue never owns `+0x668/+0x66c`. This correction is generic:
it keys only on the independently accumulated active direct-positive winner and
contains no case, hash, retry, feature-index, or action-specific condition.

The smallest source implementation can share the same active-winner publication
used by the ordinary direct-finalization branch. For byte/evidence parity it
must publish the active winner's strict metric count as well as its routed
affine. If no active relation winner exists, rescue must leave the initialized
relation evidence alone rather than synthesize it from the rescue selection.

An ephemeral end-to-end control changed only current match publication before
ordinary queued study: relation count `0 -> 10` and relation affine
`[256,-16,3348,16,256,-2342] -> [254,-35,5792,35,254,-2496]`. The unchanged
current study path still selected action 4 and produced 487055 bytes, but its
SHA-256 changed from the current
`c3291376c0339ac5b1c6e958f2c4af3ef5ec0d4e349704ff44d62cb094cd3fbc` to the
exact approved
`e1094cc9a9a08af3185bb8f41d4c86300eefd831cf9652600e4e84e6e1208f0a`.
This confirms that no second study/lifecycle/packing correction is hidden behind
the evidence-owner fix.

## Why Existing Controls Missed It

Earlier relation-winner controls exercised ordinary direct finalization,
including strict greater, equality, lower-count, inactive, and routing cases.
They did not place a successful strong `FUN_18005d9e0` rescue after a distinct
active relation winner. Earlier rescue controls focused rescue selection,
score, selected-transform retention, and lifecycle-mask handoff. They did not
assert preservation of independent relation evidence into a later action-4
study candidate. Thus each half was controlled, but their natural composition
was not.

This row is the missing mixed boundary:

```text
feature 33 direct-positive -> owns +0x668/+0x66c
feature 8 aggregate rescue -> owns score/+0x648/+0x650 and gates
feature 39 action-4 target -> consumes preserved +0x66c in slot 742
```

## Retry Prelude

The retry prelude is not causally relevant to this mismatch. It is part of the
captured chronology and must remain in strict replay input, but an ephemeral
negative control starting target frame 305 from reset preprocessing state,
without frame 304, produced the same target quality/coverage `89/99`, processed
hash, probe hash, three candidate states, selected feature 8, score 26, direct
lifecycle mask, feature-33 routed affine, and current feature-8 overwrite.

Both official and current strict replays include the prelude and are exact
through after-match. The causal defect occurs later, when current rescue
publication overwrites relation evidence that the native rescue does not touch.
No retry/preprocess special case is justified.

## Confidence

Confidence is high. The transform origins and arithmetic reproduce both six-word
outputs exactly; Ghidra establishes disjoint writer sets for direct relation
publication and aggregate rescue; action-4 target slot 742 is identified from
the unpacked candidates; and the final byte diff contains no other payload
change.
