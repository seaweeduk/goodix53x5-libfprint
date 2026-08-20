# Profile-9 / Type-12 Relation Matrix And Projection

## Scope And Identity

This contract covers relation reconstruction and reference-star serialization in
`GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
`6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`, and
the corresponding production implementation at `c182b140`.

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

Enrollment 1 sequence 3 after study is a natural sparse-star witness with ten
features, reference 0, registration count 46, and six projected records. Its
relation stream SHA-256 is
`471967d347ee000f8a2ed3745414961abdab591103f95f2d73c56a450f7597b0`.
The reconstructed records and exact wire bytes are:

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

## Natural Validation Boundary

The maintained natural enrollment stages exercise graphless and established
states without synthetic relation input:

| Witness | Features / registration | Graph | Projected slots |
| --- | --- | --- | --- |
| enrollment 1 stage 1, SHA-256 `3a313458317850dc382af95a419ac80df5122d58abcf1e9133ec374df45c028f` | `1 / 1` | `f2=-1`, `f5=0` | none |
| enrollment 1 stage 5, SHA-256 `f1ef9a22abecca5eba3adb5a4fb7e815c12bd74a863cc620f73e119db3d46196` | `5 / 11` | `f2=0`, `f5=1` | `[7]` |
| enrollment 2 stage 3, SHA-256 `accbce6791399ed1e72dcb7cb0c841de52da7637147421104f4e9405d3ceb64c` | `3 / 4` | `f2=1`, `f5=1` | `[1,3]` |

Natural loaded/studied projection witnesses span both observed references and
sparse/full production stars:

| Witness | Features / registration / reference | Relation slots | Relation-stream SHA-256 |
| --- | --- | --- | --- |
| enrollment 1 sequence 2 loaded | `8 / 29 / 0` | `[7]` | `279d334a4865887f2682a8b21649ef6fd8960c9725491c71e8cb9eb1f25c13f1` |
| enrollment 1 sequence 2 after study | `9 / 37 / 0` | `[7]` | `279d334a4865887f2682a8b21649ef6fd8960c9725491c71e8cb9eb1f25c13f1` |
| enrollment 1 sequence 3 after study | `10 / 46 / 0` | `[1,2,7,22,29,37]` | `471967d347ee000f8a2ed3745414961abdab591103f95f2d73c56a450f7597b0` |
| enrollment 1 sequence 35 after study | `40 / 781 / 0` | all 39 reference-0 slots | `500b1cc7e40b9581c58b5dfec5ed2ee4595fbc5ae2476d3dfe1abf590d5e62d6` |
| enrollment 2 sequence 35 after study | `40 / 781 / 1` | 38 defined reference-1 slots | `6aefad8be96a166a9b949af7e1c305b1b5bfbe34f70aa71bc6565d1607f3b26c` |

The natural parity comparison stream used for this audit has SHA-256
`b0f27148aa0da69536a5a385bf61a9de022c6fc2c1b1c850444d91cf161a847f`.
Its sealed source inventory predates the current source split, so it supplies
the natural DLL/output corpus rather than build provenance for `c182b140`.
Across both 399-operation enrollment chronologies:

- all 798 loaded raw-gallery templates are byte-identical to the DLL outputs;
- all 798 after-study templates are byte-identical to the DLL outputs;
- all 1,596 templates have canonical row bases and exact triangular registration
  counts, references 0 or 1, unique strictly ascending relation indices, and
  byte-exact reconstruct/project/pack relation streams;
- feature counts span 8 through 40 and relation counts span 1 through 39;
- observed relation ranks span 0 through 27 and affine dwords span -47,838
  through 39,744.

Current-source review at `c182b140` against this complete natural boundary found
no production-reachable relation reconstruction or projection difference.
