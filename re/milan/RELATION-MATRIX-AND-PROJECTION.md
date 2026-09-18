# Profile-9 / Type-12 Relation Matrix And Projection

## Scope And Identity

This contract covers relation reconstruction and reference-star serialization in
`GoodixEngineAdapter.dll` 2.0.310.900. The corresponding matrix and projection
owners are in `milan/relations.c`; study orchestration is in
`milan/study/study.c`.

The relevant DLL owners are:

| Function | Role |
| --- | --- |
| `FUN_18003e3a0` | Decode the packed template and reconstruct the live relation table |
| `FUN_18003f740` | Decode one packed 28-byte relation value and its separate slot index |
| `FUN_1800456b0` | Append a feature row and initialize its incident slots |
| `FUN_1800452e0` | Clear all relations incident to a replaced nonreference feature |
| `FUN_180045d40` | Store one feature/reference relation or update the reference star |
| `FUN_180046430` | Apply retained-feature relation updates |
| `FUN_18003f510` | Count projected relations for packed-size calculation |
| `FUN_18003eaf0` | Project and serialize the reference star |

Transform route lookup, transitive closure, and replacement/reanchoring policy
are separate contracts. This document covers the matrix representation consumed
by those operations and the projection boundary after they finish.

## Counts And Triangular Rows

For physical feature index `i`, the canonical row base is:

```text
B(0) = 0
B(i) = 1 + i*(i-1)/2, i > 0
```

Feature `i` owns the `i` pair slots `B(i)..B(i)+i-1`, one for each lower
feature `0..i-1`. For distinct features `a` and `b`:

```text
high = max(a, b)
low  = min(a, b)
slot = B(high) + low
```

The packed feature scalar `b6`, live feature offset `+0x114`, carries `B(i)`.
The outer tag `92`, live template offset `+0x24`, is the next free slot, not the
number of serialized relations:

```text
registration_count(n) = B(n) = 1 + n*(n-1)/2
valid pair slots       = 1..registration_count(n)-1
```

The row and capacity boundaries are:

| Feature or count | Row base / next slot | Owned pair slots |
| --- | ---: | --- |
| feature 0 | 0 | none |
| feature 1 | 1 | 1 |
| feature 2 | 2 | 2..3 |
| feature 3 | 4 | 4..6 |
| feature 39 | 742 | 742..780 |
| 40 features | 781 | 1..780 |
| feature 49 | 1177 | 1177..1225 |
| 50 features | 1226 | 1..1225 |

The generic live layout reserves 50 feature pointers and all 1,225 unordered
pairs. Production profile 9 fixes packed `maximum_features` at 40, so a valid
persisted print reaches at most slot 780. The wider 50-feature layout remains
the DLL codec and in-memory allocation boundary.

Append does not recompute prior rows. `FUN_1800456b0` assigns the new feature's
`b6` from the current `+0x24`, initializes exactly the old feature count's new
slots, and advances `+0x24` by that old count. On admitted production input this
is exactly `B(n) -> B(n+1)`. Production `goodix_milan_relation_matrix_append_row`
additionally requires the supplied row base to equal `B(n)` before changing the
matrix.

## Physical Slot Mapping

The DLL relation table begins at live template `+0x1b8`. A physical record is
selected as:

```text
native_record(slot) = template + 0x1b8 + slot*0x1c
```

`FUN_18003e3a0` initializes 1,226 physical records, indices `0..1225`.
Physical index 0 is the permanently unused sentinel between one-based pair
indices and the table origin; it is not a 1,226th logical pair or end slack.
The final valid record, index 1225, ends immediately before graph field
`+0x87d8`.

`GoodixMilanRelationMatrix` stores only the 1,225 logical pair records. Its C
mapping deliberately removes native index 0:

```text
C slots[slot - 1] <=> native template+0x1b8 + slot*0x1c
```

`goodix_milan_relation_matrix_slot_index` rejects equal endpoints, endpoints
outside the current feature count, and results outside `1..1225`.
`goodix_milan_relation_matrix_slot` accepts the same one-based physical range
and returns `slots[index-1]`.

## Slot Value And Direction

Every relation value is seven signed 32-bit words:

```text
[rank, a00, a01, tx, a10, a11, ty]
```

The unset value is exactly:

```text
[-1, 256, 0, 0, 0, 256, 0]
ffffffff000100000000000000000000000000000001000000000000
```

The rank is also the definition marker. A negative rank is unset and omitted
from type-12 projection. Rank zero is a defined reference edge and is serialized
even when its affine is identity. Positive ranks retain relation-construction
evidence. All seven words, including rank and translations, remain full-width
signed dwords in memory and on the wire.

The six affine words always use the canonical direction from the higher feature
index to the lower feature index. A supplied feature-to-reference affine is
copied directly when `feature > reference`; when `feature < reference`, it is
inverted before storage. `goodix_milan_relation_matrix_store_reference` sets the
stored rank to zero. Neither storage nor projection narrows, reranks, or
normalizes the seven words.

`goodix_milan_relation_matrix_clear_incident` visits every other physical
feature and restores the complete 28-byte unset value in each pair slot. It does
not change row bases, feature count, or graph metadata.

## Reconstruction And Admission

The DLL decoder first initializes all physical records, including index 0, to
the unset bytes. Each packed tag-`96` relation then supplies an `e3` slot and
seven dwords; `FUN_18003e3a0` copies the value directly to the indexed live
record. The decoder does not derive a slot from feature endpoints and does not
require packed records to be ordered. Repeated `e3` indices overwrite the same
record in encounter order, so the last duplicate is the live value. These are
decoder mechanics, not producer guarantees: natural type-12 writers emit unique
ascending reference-star records.

Production `goodix_milan_relation_matrix_init` is the semantic admission layer
for persisted prints and study:

| Field | Required production contract |
| --- | --- |
| feature count | `1..50` in the matrix; persisted profile-9 validation further limits it to 40 |
| registration count | exactly `1 + n*(n-1)/2` |
| row bases | every present feature has exactly `B(i)` |
| graph flag | exactly 0 or 1 |
| reference | in range when established; `-1` or in range when unestablished |
| graphless relations | none |
| relation index | `1 <= index < registration_count`, hence a pair among present features |
| relation endpoint | the indexed pair must be incident to the graph reference |
| relation rank | nonnegative |
| duplicates | rejected by physical slot index |

The matrix initializer accepts unique valid star records in any input order and
copies them without sorting. `goodix_milan_print_validate_template` also
re-encodes the input relation array in its existing order, so reordered unique
star records are accepted even though no natural producer emits them. Study and
enrollment projection replace that order with the native ascending traversal.

No producer or initializer requires a complete star. An established graph may
omit any nonreference edge; reconstruction leaves that star slot at the unset
value. Active-feature policy can impose stronger invariants at individual call
sites, but activity is not part of matrix admission or serialization.

## Reference-Star Projection

The type-12 serializer projects only the live graph reference's incident slots:

```text
if graph_established <= 0:
    relation_count = 0
else:
    for feature in 0..feature_count-1:
        if feature == reference:
            continue
        slot = B(max(feature, reference)) + min(feature, reference)
        if matrix[slot].rank >= 0:
            emit slot and all seven stored dwords verbatim
```

Feature-index traversal makes emitted `e3` slots strictly increasing for
canonical row bases, including when the reference is nonzero. Omitted unset
slots do not leave placeholders. The relation count is exactly the number of
emitted records; no separate relation-count field exists on the wire. The
decoder counts consecutive tag-`96` records until graph tag `93`.

The generic type-12 projection bound is 49 records, one for every nonreference
feature in a 50-feature object. Production
`goodix_milan_relation_matrix_project_reference_star` enforces both caller
capacity and this literal 49-record bound. A persisted profile-9 print has at
most 40 features and therefore emits at most 39. A sparse star may emit any
smaller count, including zero for a valid established matrix whose star is
entirely unset.

`FUN_18003f510` and `FUN_18003eaf0` use the identical rank and traversal
predicate. Every emitted relation therefore contributes exactly 45 bytes to
both the size and packed output.

## Exact Packed Record

One relation is exactly 45 bytes:

```text
96 28 00 00 00
e3 <slot, little-endian signed dword>
e1 <rank, little-endian signed dword>
e4 <a00,  little-endian signed dword>
e5 <a01,  little-endian signed dword>
e6 <tx,   little-endian signed dword>
e7 <a10,  little-endian signed dword>
e8 <a11,  little-endian signed dword>
e9 <ty,   little-endian signed dword>
```

For example, a sparse star with ten features, reference zero, and registration
count 46 can encode these six records without emitting its missing slots:

| Slot | Seven signed dwords | Exact 45-byte record |
| ---: | --- | --- |
| 1 | `[0,256,8,-14036,-8,256,-16943]` | `9628000000e301000000e100000000e400010000e508000000e62cc9ffffe7f8ffffffe800010000e9d1bdffff` |
| 2 | `[0,255,-24,12468,24,255,-16228]` | `9628000000e302000000e100000000e4ff000000e5e8ffffffe6b4300000e718000000e8ff000000e99cc0ffff` |
| 7 | `[16,253,58,3166,-51,252,1633]` | `9628000000e307000000e110000000e4fd000000e53a000000e65e0c0000e7cdffffffe8fc000000e961060000` |
| 22 | `[0,256,-1,1303,1,256,-10353]` | `9628000000e316000000e100000000e400010000e5ffffffffe617050000e701000000e800010000e98fd7ffff` |
| 29 | `[0,255,30,1197,-30,255,-14118]` | `9628000000e31d000000e100000000e4ff000000e51e000000e6ad040000e7e2ffffffe8ff000000e9dac8ffff` |
| 37 | `[0,260,10,-8257,-15,241,-13502]` | `9628000000e325000000e100000000e404010000e50a000000e6bfdfffffe7f1ffffffe8f1000000e942cbffff` |

## Production Caller Ownership

| Production caller | Matrix contract |
| --- | --- |
| `goodix_milan_print_validate_template` | Reads all feature `b6` values, reconstructs the matrix, and rejects invalid counts, rows, graph metadata, nonstar records, negative ranks, and duplicate slots before accepting a print |
| `goodix_match_combine_templates` | Builds canonical rows, admits direct pair records into the triangular matrix, runs closure at exactly 40 features, projects the resulting reference star, and normalizes that full-capacity output |
| `milan_study_build_relation_matrix` | Reads packed `b6`, registration, graph metadata, and sparse relations into the initialized matrix before study mutation |
| `goodix_milan_study_append` | Supplies current registration count as the new row base, advances registration by the old feature count, conditionally stores the new reference edge, then projects before packing |
| `goodix_milan_study_action0_transient` | Reconstructs and projects only when retained refresh actually mutates the matrix |
| `goodix_milan_study_replace` | Reconstructs once and projects after matrix mutation and again at final packing; action-specific mutation semantics are outside this contract |

The packed metadata graph reference and graph-established flag are the matrix's
`reference_feature_index` and `graph_established`. Graph companions `f3/f4`,
feature order, queue state, and recognition-evidence relation count do not
participate in slot reconstruction or projection.

## Established-Star Study Invariant

A canonical freshly decoded established type-12 gallery starts with every
nonreference pair unset. Recognition and study preserve that property:

- Append initializes its new row to unset. `FUN_180045d40` then writes only
  the new feature/reference slot.
- Nonreference replacement clears all target incidents. Actions 3/4 reinstall
  only the target/reference slot; action 2 leaves them unset. Nonreference
  retained refresh clears the incidents and writes only the reference slot.
  Reference replacement or refresh either preserves its incidents or composes
  only already-defined reference incidents, retaining their leading words.
- `FUN_180047120` reads feature/reference slots in both directions, composes
  them into temporary directed footprints, and writes residuals and live
  overlap counts. It does not store pairwise footprint transforms in the matrix.
- The primary dispatcher and queued selector do not run enrollment graph closure.

Consequently reference-star projection followed by matrix reconstruction loses
no defined relation of an established-star gallery during these operations.
This statement is independent of affine magnitude: it is about slot ownership,
not the numerical equivalence of inversion, composition, or rasterization.
It does not apply to a live enrollment matrix containing non-star edges, or to
a graph-unestablished object with a stale reference and retained refresh before
first anchoring; those require their own producer contracts.

Reconstruction of relations is also distinct from reconstructing the entire live
gallery. Packed records omit live fields; fresh decode repairs partition endpoints
and normalizes residuals and the nonserialized `+0x148` counts. Deferred queue
bodies/ranks are absent from the wire. A reference-star equality therefore does
not authorize replacing retained feature, count, or queue owners by fresh decode.
See `functions/FUN_180044fc0.md`, `functions/FUN_1800469f0.md`, and
`functions/FUN_18005d330.md` for their independent lifetimes.

### Translation-Only Normalization Domain

If every defined star affine and incoming study affine has linear part
`[256,0;0,256]`, inversion and composition preserve that linear part. Translation
negation and addition retain their low dwords. This includes repeated reference
replacement and retained refresh; no bound on the number of such operations is
needed to preserve the linear part.

`FUN_180047120` composes the directed pair before arithmetic-shifting its two
translations by one. Each resulting translation is therefore in
`[-1073741824,1073741823]`. On the `52x44` footprint domain, adding `256*x` or
`256*y`, and subtracting either source endpoint, cannot overflow a signed dword.
The two `FUN_180073090` calls made by `FUN_180071370` reduce to a slope-256
horizontal interval and a slope-zero vertical admission test. Accepted rows are
contiguous and all consumed row intervals have been written. The cleared cell
set is exactly the translated rectangle intersected with the destination domain,
independent of the original binary mask or earlier removals.

This bounds the half-resolution normalization and composed selector footprints.
It does not bound the selector's separate unhalved primary-affine footprint or
arbitrary affine normalization: `FUN_1800687c0` wraps its square sum and biased
numerators, so its name alone is not a guarantee that every output coefficient
has magnitude at most 256.
