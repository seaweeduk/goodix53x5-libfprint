# Profile-9/Type-12 Study Relation Mutation

## Authority And Boundary

- Production source: `c182b140a86e70312bd9f637ba68cf8a9140f6c0`.
- DLL: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Native call path: `templateStudy -> FUN_180044fc0 -> FUN_180046430 /
  FUN_1800469f0 -> FUN_1800458c0 -> FUN_1800452e0 / FUN_180045d40`.
- Current-source path: `goodix_match_study_feature_internal()` into
  `goodix_milan_study_append()`, `goodix_milan_study_replace()`, and the ordered
  `milan_study_refresh_retained()` prepass.

The retained strict report
`candidate-admission-c182b140-natural-643-20260820.json` compares current source
with the approved DLL at the exact after-match, study-action, and final-candidate
boundary. All 643 selected operations and all 1,104 gallery rows are exact. The
503 positive study results comprise 56 action-1 appends, five action-2
replacements without relation installation, and 442 action-4 replacements.
Actions 3 and 5 do not occur in this authority.

The checker compares every gallery row's complete after-match template SHA-256,
the study action, and the complete final-candidate SHA-256. The decoded relation,
active-marker, and graph statements below are therefore fields inside templates
whose complete bytes agree, not weaker field-only comparisons.

## Study And Closure Boundary

`FUN_18007a040` is not on the profile-9/type-12 `templateStudy` path.

- `FUN_18003e3a0:0x18003e8ac..0x18003e8bd` calls closure only when
  `(live_type - 9) & 0xfffffff6 == 0`, selecting live types `9`, `10`, `17`, and
  `18`; type 12 skips the call.
- The other study-reachable callers are below `FUN_18007b310`, which both the
  primary dispatcher and queued helper use only for those same legacy types.
  Type 12 selects `FUN_1800469f0` instead.
- At production `c182b140`, `goodix_milan_relation_matrix_close()` is called
  only by the separate 40-feature enrollment finalizer. Study reconstruction
  leaves every omitted non-reference slot at the unset identity sentinel.
- `goodix_milan_enrollment_feature_to_reference()` likewise has no study caller;
  its only production source caller is enrollment bridging.

Consequently no successful natural type-12 study can attribute a `b5` change or
a rank-two edge to closure. Study-time active changes are owned by retained
refresh, first-graph append, matched-active inheritance, or action 2 as described
below.

## Matrix And Direction

For feature indices `i != j`, let `high=max(i,j)` and `low=min(i,j)`. The
canonical slot is:

```text
slot = feature[high].b6 + low
```

Its six affine dwords always map `high -> low`. A relation record is seven signed
dwords: one leading rank followed by Q8 affine
`[a,b,tx,c,d,ty]`. The maintained study meanings are:

- `-1` plus identity affine: unset;
- `0`: a retained-refresh, append, or replacement relation materialized by
  study;
- positive input rank: matcher/enrollment-owned direct evidence retained in the
  live matrix;
- `2`: an edge synthesized by `FUN_18007a040`, outside type-12 study.

Every read or store that requests `second -> first` uses the canonical affine
directly when `second > first` and its inverse when `second < first`.
`compose(A,B)` is `A after B`, or matrix product `A*B`, and normalizes the linear
part after every composition.

## Retained Refresh

`FUN_180046430` and `milan_study_refresh_retained()` consume retained entries in
their supplied order. For entry index `i`, retained affine `T`, and evidence
relation affine `E`, they compute:

```text
X = compose(E, inverse(T))
```

The entry's `b5` is written to literal one before relation mutation.

For `i != reference`, every incident slot is reset to
`[-1,256,0,0,0,256,0]`, then the `i/reference` slot receives leading zero. It
stores `X` directly when `i > reference` and `inverse(X)` when `i < reference`.
Thus the semantic relation is `i -> reference` while storage remains
`high -> low`.

For `i == reference`, no slot is cleared and every defined reference-star rank
is preserved. With supplied new-reference-to-old-reference affine `X`:

```text
lower feature:  stored old-reference->feature S = compose(S, X)
higher feature: stored feature->old-reference S = compose(inverse(X), S)
```

The primary dispatcher runs this prepass only for positive retained count and
retained flag exactly one. First-graph append can run a second ordered pass after
installing the matched feature as the reference; that second pass requires only
positive count. These passes are distinct when the original graph is
unestablished.

In the retained authority, eight action-1 candidates and four full-capacity
action-4 candidates contain observable retained refresh. No retained reference
entry exercises the lower-feature reanchor branch.

## Append And Replacement

Action 1 appends one row of unset slots. If the graph was unestablished, it sets
`f2` to the matched feature, sets `f5` to one, and sets matched and appended
`b5` to one. Otherwise the appended `b5` copies the matched feature's
post-refresh dword exactly. A reference edge is installed only when the old graph
was unestablished or matched `b5` equals exactly one. Its leading rank is zero.

Actions 3 and 4 call the same relation replacement helper after the feature copy.
For a nonreference target, the copy helper clears every incident slot and the
relation helper installs exactly one leading-zero target/reference edge. For a
reference target, incident slots are retained and every defined star affine is
reanchored with ranks unchanged. The relation helpers do not write `b5`; actions
3 and 4 preserve the target's active marker unless retained refresh changed it
earlier.

Action 2 calls the same feature copy and incident clearing but does not call the
relation installer. A nonreference target therefore loses its reference-star
record and every live non-star incident edge; a reference target retains its
star. The caller then writes target `b5=0`.

The native mutation helpers are `void` and have no rollback or recoverable
failure status. Current source validates pointers, indices, row bases, and
packing, returning `-1` on invalid internal state and publishing no candidate.
On valid successful inputs, refresh and replacement return zero; closure returns
zero or one as defined below.

## Exact Natural Transitions

### Retained Refresh Plus Append

Selector `da0f8d98-6e9e-4cee-99d5-f7109e402baf/verify/4` has exact
after-match SHA-256
`1589767046239252245e887af2ac62c9b6f6a674d6882303c3b923fbb0bcb1ff`
and action-1 candidate SHA-256
`dc8a96c70763a7aa489d2a2fddb9298df742e10bbe41dd3a697414543fb97af5`.

The after-match object has 12 features, five relations, graph
`f2/f3/f4/f5 = 0/-1/-1/1`, and active markers:

```text
[1,0,1,0,0,0,1,0,1,1,1,0]
```

Its complete relation map is:

```text
2  = [8,257,12,-6501,6,247,-13263]
16 = [0,256,7,-2481,-7,256,-5897]
29 = [22,255,7,4603,-13,246,3588]
37 = [0,256,10,-10029,-10,256,-8339]
46 = [20,258,-11,7849,28,246,-1203]
```

Study activates retained feature 4 and adds its reference slot, then appends
active feature 12 and its reference slot. The candidate has 13 features, seven
relations, unchanged graph fields, active markers:

```text
[1,0,1,0,1,0,1,0,1,1,1,0,1]
```

The five prior records are byte-identical and these are added:

```text
7  = [0,256,-2,-4863,2,256,-5447]
67 = [0,248,48,-61,-41,249,-6806]
```

### Nonreference Action 4

Selector `0e385ab2-63b3-4891-aa1c-6b6af49b9154/identify/1` has exact
after-match SHA-256
`ef196974e231d48b5046b77e9c3395be374ab1f242c60dbdf5cd1c780523bfcc`
and action-4 candidate SHA-256
`973e0878d94c57a01179c867e758c9f2d8cfd805608577c437a2528c311eaabc`.
Both have 40 features, 38 relations, graph `0/-1/-1/1`, and active markers:

```text
[1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]
```

Every relation record is byte-identical except target/reference slot 137:

```text
before = [0,255,25,15177,-25,255,-12064]
after  = [0,256,16,-2364,-16,256,-44499]
```

### Full-Capacity Refresh Plus Action 4

Selector `2ac1a71b-c5ce-4035-8deb-9143d542202d/identify/36` has exact
after-match SHA-256
`70518bbb91bec7f78abe5a6a166640d1e7caa0036621ce4fedb7d202c733baee`
and action-4 candidate SHA-256
`ef5c61711ce44b73432c6cb2fddc8752adca8608bfaa8d39223b1df9409248d3`.
Both have 40 features and graph `0/-1/-1/1`.

Retained refresh changes feature 27 `b5` from zero to one and adds:

```text
352 = [0,222,-128,51631,128,222,-38734]
```

The nonreference replacement changes only its own reference slot:

```text
631 before = [0,256,13,-6315,-13,256,-3469]
631 after  = [0,254,-36,33833,36,254,-29650]
```

All other relation records, active markers, and graph fields are byte-identical.
The packed relation count is `35 -> 36`.

### Reference Action 4

Selector `0e385ab2-63b3-4891-aa1c-6b6af49b9154/verify/184` has exact
after-match SHA-256
`d03809194744154a56f58ebcae2bfef39ff2a97c3de7f766fe94d054637b3f91`
and action-4 candidate SHA-256
`2e8610edbff23127677e90400471437ca9de2656dbb824cad699ed62ec070349`.
Reference 1 is replaced. Both objects retain 40 features, 36 relations, graph
`1/-1/-1/1`, and active markers:

```text
[0,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]
```

The candidate's complete reanchored relation map is:

```text
3   = [0,256,3,46258,-3,256,-9543]
8   = [0,256,1,34513,-1,256,10217]
12  = [0,242,86,10979,-86,242,-20815]
17  = [0,243,80,7466,-80,243,27827]
23  = [0,256,-12,36634,12,256,1088]
30  = [0,256,19,29810,-19,256,824]
38  = [0,236,100,6120,-100,236,-22755]
47  = [0,250,57,11934,-57,250,-22628]
57  = [14,256,9,20167,-9,256,6162]
68  = [0,248,65,9764,-65,248,-4616]
80  = [0,250,56,20044,-56,250,-1766]
93  = [0,256,7,19013,-7,256,-7131]
107 = [0,246,73,13761,-73,246,-9789]
122 = [0,256,-4,23280,4,256,13167]
155 = [0,244,77,9140,-77,244,-7556]
173 = [0,238,94,16965,-94,238,-18316]
192 = [0,254,37,-2904,-37,254,3493]
212 = [0,246,71,12762,-71,246,6493]
233 = [0,245,76,10602,-76,245,-4516]
255 = [0,247,70,-10755,-70,247,-9146]
278 = [0,246,71,29424,-71,246,10549]
302 = [0,245,76,15944,-76,245,6965]
327 = [0,256,16,2578,-16,256,5648]
353 = [0,254,38,22527,-38,254,20888]
380 = [0,246,72,20286,-72,246,-21637]
408 = [0,249,60,15825,-60,249,13944]
437 = [0,255,23,35637,-23,255,13733]
467 = [0,256,11,46820,-11,256,3951]
498 = [0,248,64,9163,-64,248,14799]
530 = [0,256,-12,54918,12,256,11759]
563 = [0,252,46,27761,-46,252,6060]
597 = [0,247,70,13171,-70,247,-620]
632 = [0,253,40,24161,-40,253,5023]
668 = [0,256,-11,48148,11,256,2158]
705 = [0,256,0,39914,0,256,-11458]
743 = [0,247,70,19280,-70,247,-21763]
```

Slot 1, the only lower-feature/reference slot, is absent. All 36 defined slots
therefore exercise the higher-feature direct composition branch. The authority
contains five additional reference replacements; each uses reference zero and
therefore also has no lower side.

### Nonreference Action 2

Selector `0e385ab2-63b3-4891-aa1c-6b6af49b9154/identify/12` has exact
after-match SHA-256
`6b4e4eb192d34d223183b2aa4fbb3167f3b43cd23d72e0228cd46a1fa19c4ad9`
and action-2 candidate SHA-256
`9d6a49daebcaa2fcf093ef947a3fc255a93c7391b183629936de3b14f55a81ca`.
Both retain 40 features and graph `1/-1/-1/1`. Feature 17 changes `b5` from one
to zero and its reference slot is removed:

```text
138 before = [0,249,65,-15939,-65,249,-4420]
138 after  = unset and omitted
```

The packed relation count is `37 -> 36`; every other relation and active marker
is byte-identical. All five natural action-2 candidates in the authority have
the same nonreference one-slot removal and one-marker clearing shape.

## Closure Contract

For its null-external real-vertex mode, `FUN_18007a040` and
`goodix_milan_relation_matrix_close()` process roots in ascending order. Each root
resets its visited vector, pushes the root, and consumes a LIFO worklist. Every
popped current vertex scans candidates in descending order. Because descending
candidates are pushed in that order, the lowest newly admitted candidate is
popped next.

Only a strictly positive edge is traversable. Reads orient the root/current edge
as `current -> root` and the current/candidate edge as `candidate -> current`.
Their composition produces `candidate -> root`; stores invert only when required
by canonical high-to-low storage.

The 51-by-51 path-quality matrix is zeroed once per call, not once per root. On
every non-root pop, the visible root/current rank overwrites the corresponding
quality cells. A discovered edge writes its direct rank, and the candidate
bottleneck is:

```text
min(visible root/current rank, current/candidate rank)
```

A direct root/candidate rank below two is replaceable. Rank two is replaceable
only when the stored quality is strictly less than the new bottleneck; equality
keeps the existing transform. A rank above two is never replaced. Publication
writes rank two and the composed affine immediately, so later traversal sees the
new edge. Once a synthesized edge is popped, its visible rank two replaces the
earlier bottleneck as the next-hop root quality.

In real-vertex null-external mode, a positive direct root edge claims its
candidate during the root's first scan. The visited rule prevents an alternate
path from replacing that rank-one or rank-two direct edge. The generic rank-two
strict-improvement branch is therefore not reached for such direct pairs; first
discovery owns synthesized unset/rank-zero pairs.

Active propagation occurs only after a rank-two publication and only for two real
vertices. If either endpoint is exactly one, both become one. Other nonzero
values do not satisfy the predicate. A direct positive edge does not propagate
active state. The return is one when at least one relation is published and zero
otherwise; it reports relation change, not active change. The DLL has no error
return. Current source additionally returns `-1` for invalid pointers, counts,
row bases, slot lookup, or worklist state.

As a direct helper control, the canonical action-1 after-match gallery above
starts with five defined star slots. Closure returns one and adds exactly:

```text
31 = [2,256,8,10256,-8,256,17210]
48 = [2,256,-21,13714,21,256,12174]
54 = [2,255,-29,3369,29,255,-4824]
```

Its active vector is unchanged because all three positive-star endpoints are
already active. These are valid helper outputs, not `templateStudy` mutations;
the type-12 caller exclusion above prevents their creation in the natural study
transaction.

## Lower-Side Reanchor Applicability

Production `c182b140` is byte-exact for the naturally exercised higher side.
Its generic lower-side implementation obtains the algebraically equivalent route
by invert-compose-invert, whereas the DLL directly computes `compose(S,X)`.
Integer inversion and normalization make those routes byte-distinct. The valid
discriminator remains:

```text
S = [241,-88,25127,88,241,-13811]
X = [268,74,-16487,-74,246,18204]
DLL direct compose      = [256,-16,3348,16,256,-2342]
c182b140 canonical route = [254,-17,2979,17,254,-2364]
```

No retained natural operation supplies a defined lower slot while replacing or
refreshing a nonzero reference. The discriminator is therefore a generic
synthetic-valid contract difference, not an observed production candidate
difference under the retained authority.
