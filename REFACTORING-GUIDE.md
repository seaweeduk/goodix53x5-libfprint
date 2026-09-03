# Refactoring Guide: Improving The Codebase Without Losing Native Parity

This guide is for developers who want to make `drivers/goodix53x5/` easier to
read, navigate, and change while keeping the byte-exact agreement with the
native Milan implementation that the project has established. It explains why
the code looks the way it does, which safety nets exist, what kinds of change
are safe, how to prove a change is safe, and which improvements are worth
making first. The worked example at the end,
`drivers/goodix53x5/milan/antifake/morphology.c`, is the reference for how a
refactoring PR should look.

The scope is the hardware this project actually supports: sensor type 12,
native profile 9, on the `27c6:5335` family. Improvements are judged by whether
they make that driver better, not by how general they are.

## 1. Why The Code Looks Like This

The Milan implementation was recovered from `GoodixEngineAdapter.dll` with
Ghidra and re-implemented by hand. It is not decompiler output: there are no
`uVar1`, `local_28`, `param_1`, or `FUN_1800xxxxx` identifiers anywhere in
`drivers/`, the style follows libfprint's GNU conventions, and every function
validates its arguments. The difficulty is structural, and it has a specific
shape.

### 1.1 Is there "too much code"?

`drivers/goodix53x5/` is about 37,000 lines of C across 725 functions. A large
part of that volume is a direct consequence of how the code was recovered:

- **Native control flow was traced one function at a time.** Each native
  routine became one C function with the same inputs, outputs, and ordering,
  because that is what could be verified against the DLL. Where native did
  the same work twice (for example the forward and reverse match passes in
  `milan/match/match.c`), the C code does it twice too. The expressions
  `overlap_coverage * 246 >> 8`, `51 * 0x100`, and `direct_metrics[4] < 207`
  each appear in both passes.
- **Native memory layouts were kept as anonymous storage.** The matcher
  dispatcher input is `int32_t metrics[77]`, read and written by slot number
  (`direct_metrics[5]`, `reverse_metrics[9]`). The anti-fake result is a
  6,844-byte blob with offset macros and `memcpy` accessors. Preprocess state
  carries 79 `_Static_assert`s in `milan/milan.h` pinning field offsets such as
  `137840` to the native struct even though that struct is never serialized as
  a unit (`device/persistence.c` encodes fields individually). Layout mirroring
  made Ghidra cross-checking easier at the time, but it is now the main thing
  standing between a reader and the meaning of the code.
- **Defensive checks were added everywhere native has none.** There are about
  600 early-return guard clauses. Native faults on bad input; the driver must
  not. These are correct and should stay, but they cost lines.
- **Debug and parity instrumentation is interleaved with the algorithm.**
  `milan/match/match.c` alone has 26 `GOODIX53X5_DEBUG`/`GOODIX53X5_PARITY`
  sites.
- **The GNU brace style is verbose.** Roughly 4,300 lines are a lone brace and
  about 1,000 more are one-parameter-per-line signature continuations. That is
  15% of the file before any logic is read. It is also libfprint's house style
  and is not going to change.

So the answer is: yes, a lot of the volume comes from reproducing native
structure function-by-function and layout-by-layout, and no, most of it cannot
simply be deleted. It can be reshaped. A readability refactor in this style
often makes a file *longer* (the worked example grows from 300 to 547 lines,
102 of them comments) while making it far cheaper to understand. Line count is
the wrong metric; the metrics that matter are in section 8.

### 1.2 The concrete pain points

Measured on `main` at the time of writing:

| Symptom | Evidence |
| --- | --- |
| Very long functions | 27 functions over 150 lines, 7 over 300. `milan_match_prepared_probe` (`milan/match/match.c:694`) is about 1,250 lines with nesting 12 levels deep. Others: `goodix_open_ssm_handler` 454, `goodix_debug_log_runtime_result` 412, `goodix_scan_coordinator_handler` 392, `goodix_milan_profile9_build_broken_mask` 377, `goodix_milan_runtime_run` 368. |
| Almost no explanatory comments | 1,980 comment lines in 37,000 (5%). `milan/core.c` has 49 comment lines in 2,813; `milan/enrollment/template.c` has 12 in 1,289; `milan/match/correspondence.c` has 12 in 977. |
| Unnamed constants | 462 hex literals of three or more digits outside `#define`/`_Static_assert`. Fixed-point factors such as `* 291 >> 16` (a 15x15 box average), `* 246 >> 8`, thresholds like `0x3333`, `0x78`, `207`, `195`, and label sentinels `0xfd`/`0xfe`/`0xff` are written inline. |
| Unnamed slots | `metrics[77]`, `direct_metrics[n]`, `relation_values[n]`, `vector[51]`: meaning is only recoverable from the RE notes. |
| God header | `milan/milan.h` is 1,404 lines and is included by nearly every Milan file, test, and tool. |
| Inconsistent prefixes | `goodix_milan_*` (241), `goodix_*` (218), `milan_*` (111), `goodix_match_*` (36) are used for overlapping subsystems; `milan/match/*` mixes `goodix_milan_` and `goodix_match_`. |
| Scratch buffers in public signatures | `goodix_milan_antifake_boundary_score (..., uint8_t *thinned, ...)` makes the caller allocate a work buffer it never reads. |
| Hardcoded profile selection | `chip_type == 12 ? 0 : -2` in `milan/antifake/construction.c`; `profile9_*` names in 12 files. |
| Duplicate build lists | Every Milan source file is listed in both `drivers/goodix53x5/meson.build` and `tools/milan-parity/build-current-runner`. |

## 2. What "Native Parity" Means And What Protects It

Parity means: for the same inputs, the driver produces byte-identical outputs
to the native DLL at every observed boundary: processed image, quality and
coverage, probe template, per-gallery-row scores and after-match templates,
learned candidate, study action, queue state, and lifecycle flags. Everything
in this guide is about changing *how* the code is written without changing any
of those bytes.

Three independent nets catch parity breaks. Use all three for any refactor of
algorithmic code.

### 2.1 Synthetic hash tests (seconds, runs in CI)

`tests/test-goodix53x5-milan-synthetic.c` runs the pipeline on generated
frames and compares SHA-256 digests of the processed image, the extracted
feature template, and the anti-fake blob against pinned values. Any change to
any byte of those outputs fails the test. `tests/test-goodix53x5-milan-state.c`
and `tests/test-goodix53x5-milan-runtime.c` cover persistence, state machines,
and the runtime orchestration with fixed expectations.

```sh
GOODIX53X5_DEBUG=0 GOODIX_LIBFPRINT_OFFLINE=1 ./scripts/build-local.sh
meson test -C .build/libfprint/builddir --print-errorlogs \
  goodix53x5-milan-synthetic goodix53x5-milan-state goodix53x5-milan-runtime
```

These are necessary but not sufficient: a synthetic frame exercises one path
through the code. `.github/workflows/milan-tests.yml` runs exactly this on
every PR.

### 2.2 Dump replay against the native DLL (minutes, private)

`tools/milan-parity` replays captured real operations through both the DLL
(under Wine) and the *current checkout* and compares the full projection.
This is the authoritative check and the one PR descriptions cite. It needs the
private artifacts (approved DLL, Wine prefix, dump campaigns) documented in
[`tools/milan-parity/README.md`](tools/milan-parity/README.md).

```sh
GOODIX53X5_DEBUG=0 ./scripts/build-local.sh   # once, creates the support builddir
./tools/milan-parity/milan-parity validate-dump \
  --dump-dir "$CAMPAIGN/debug-dump" \
  --build-manifest "$CAMPAIGN/driver-build.json" \
  --operation SESSION/ACTION/EPOCH ... \
  --dll "$DLL" --approved-dll-sha256 "$DLL_SHA256" --wine-prefix "$WINE_PREFIX" \
  --compare-current --report "$REPORT"
```

The README contains a snippet that builds the `--operation` list for every
identify/verify operation with a non-empty gallery. The standing campaigns
range from 26 operations (about 20 seconds) to 643 operations (about 5
minutes). Run the small one while iterating and the large ones once on the
final source.

Two operational rules follow from how the tool works:

- The current-runner is compiled from the working tree and its source identity
  is pinned for the run. **Do not edit anything under `drivers/goodix53x5/`,
  `meson-integration.patch`, or `scripts/build-local.sh` while a campaign is
  running.** Documentation edits are fine.
- `tools/milan-parity/build-current-runner` has its own hard-coded list of
  Milan sources. If you add, rename, or split a `.c` file, update it *and*
  `drivers/goodix53x5/meson.build`, or the campaign will fail to link.

### 2.3 Differential testing of the old and new function (seconds, strongest)

For a pure function, the strongest evidence is to compile the pre-change file
and the post-change file into one binary and run both on many random inputs.
This checks *all* inputs the harness can generate, not just the ones present
in captured dumps, and it takes minutes to set up. It is how the worked example
was validated: 20,000 randomized cases at several geometries, plus a
sanitizer build, plus a deliberately broken mutant to prove the harness would
notice.

The recipe (keep it outside the repository, for example under `/tmp`):

```sh
git show HEAD:drivers/goodix53x5/milan/antifake/morphology.c > old.c
INC=drivers/goodix53x5
cc -std=c17 -O2 -Wall -Wextra -Werror -I"$INC" \
   -Dgoodix_milan_antifake_class_map=old_class_map \
   -Dgoodix_milan_antifake_boundary_score=old_boundary_score \
   -c old.c -o old.o
cc -std=c17 -O2 -Wall -Wextra -Werror -I"$INC" \
   -c drivers/goodix53x5/milan/antifake/morphology.c -o new.o
cc harness.c old.o new.o -o harness && ./harness 20000
```

The harness generates inputs that hit every branch you can think of (full
range values to force 32-bit wraps, sparse masks, degenerate sizes, all-zero
inputs), calls both versions, and compares return codes and every output byte.
Always finish with a **mutation check**: introduce a one-line behavioural
change into the new file and confirm the harness reports mismatches. A harness
that cannot fail proves nothing.

Adding such a harness to the repository as a test requires maintainer approval
(see `AGENTS.md`); until then it is a development tool that lives outside the
tree and its result is reported in the PR.

### 2.4 The RE notes are the specification

The `re/milan/**` notes on the `milan-dev` branch (checked out at
`../goodix53x5-libfprint-milan-dev` in a typical setup) record, per native
function, the exact predicates, arithmetic, ordering, and quirks. Before
touching a function, find its native owner and read the note. If a comment you
are about to write contradicts the note, the note wins until you have new
evidence. Notes are keyed by native address (`FUN_18003ad10.md`) or export
name (`getQuality.md`); the anti-fake, match, study, and preprocess owners are
well covered.

## 3. The Refactoring Contract

A refactoring PR changes representation, not behaviour. The following are
**allowed** without new native evidence, provided the three nets pass:

- Renaming local variables, static functions, constants, and types.
- Introducing named constants (`#define`, `enum`, `static const`) for literal
  values, and named accessors for slots in native-layout arrays.
- Splitting a long function into static helpers that are called in the same
  order with the same data.
- Extracting duplicated blocks into one helper, when the two blocks are
  textually identical after renaming.
- Adding comments, file headers, and documentation.
- Moving declarations between headers; splitting `milan.h`.
- Replacing hand-rolled idioms with an equivalent helper when the result is
  identical for all inputs, including overflow and signedness (`abs` on an
  `int` difference of two `uint8_t` values is fine; `abs` on a possibly
  `INT_MIN` value is not).
- Running `uncrustify` with libfprint's configuration.

The following **change behaviour** even when they look cosmetic, and are not
refactors. They need their own PR with native evidence:

- Changing integer widths, signedness, or the order of operations in
  arithmetic that can wrap (`difference_sum * 1000` on `uint32_t` wraps on
  purpose).
- Changing rounding, truncation, or division semantics (arithmetic vs logical
  shift, signed vs unsigned division).
- Changing iteration order, LIFO/FIFO discipline, tie-breaking, or the order
  in which neighbours are visited. In the watershed, the four-neighbour visit
  order decides which label wins a tie.
- Changing loop bounds or margins, even asymmetric ones that look like bugs
  (`columns 2..columns-3` in the boundary maximum is native behaviour).
- "Fixing" a convergence test, an off-by-one, or a sentinel value that the
  native code has.
- Removing a guard clause (the guard is driver behaviour; native would fault).
- Changing a struct whose bytes are persisted or serialized: the template
  codec, the anti-fake blob, persisted preprocess state, and anything that
  the debug dump writes as raw bytes.
- Changing the content or naming of debug dump records
  (`goodix53x5-runtime-debug/v3`); the parity tool rejects any other schema.
- Reordering side effects around a debug hook, since dumps are compared.

When a refactor uncovers something that looks wrong, leave it exactly as it is,
mark it with a comment that starts with `Native quirk:` and describes the
observable behaviour, and open an issue or an audit. Do not combine an
algorithm correction with a readability change in one commit.

## 4. Workflow For A Refactoring PR

1. **Pick one file, one concern.** The reviewable unit is one source file (or
   one header split) with no behavioural change. Do not mix a rename sweep
   across twenty files with a function decomposition in one of them.
2. **Read the native owner.** Locate the RE note(s) for every function you
   will touch. Write down the quirks the note calls out; each one becomes a
   comment in the code.
3. **Establish the baseline on `origin/main`.** Build, run the three suites,
   and run `--compare-current` on at least the small campaign. If the
   baseline fails, stop; the refactor cannot be judged.
4. **Refactor.** Prefer the transformations in section 3. Give every stage a
   name that says what it computes, not where it came from. Write a file
   header that states what the file produces, lists its stages, and names the
   native owners so a reader can find the notes.
5. **Format.** `uncrustify -c .build/libfprint/scripts/uncrustify.cfg
   --replace --no-backup FILE`, then check the result is idempotent. The
   pinned libfprint checkout under `.build/` carries the config.
6. **Differential harness** for every pure function whose body changed, with a
   sanitizer build and a mutation check.
7. **Build and suites** with `GOODIX53X5_DEBUG=0`; also build once with
   `GOODIX53X5_DEBUG=1` if you touched any file that has debug hooks.
8. **Campaigns** once on the final source identity: the small one and at
   least one large standing one. Record `selected/passed/failed` and the
   current source identity from the report.
9. **Commit hygiene.** One production commit; no `re/milan/**` paths, no
   private artifacts, no user-local files. If your reading of the native code
   produced new durable knowledge, put it in a separate documentation commit
   on `milan-dev` (see `AGENTS.md`).
10. **PR description** using the template below. The reviewer should be able
    to see, without running anything, that behaviour was not meant to change
    and what evidence supports that.

PR body template:

```markdown
## Summary
What was hard to read, what the file does now, what was renamed or split.
State explicitly: no behavioural change intended.

## Native quirks preserved
Bullet list of every deliberately odd behaviour that a reviewer might be
tempted to "fix", with the native owner for each.

## Evidence
- Differential harness: N randomized cases at these geometries, 0 mismatches;
  sanitizer build clean; mutant caught.
- Suites: synthetic/state/runtime pass.
- `--compare-current`: campaign X: a/a; campaign Y: b/b; source identity Z.

## Follow-ups (not in this PR)
Interface cleanups, header moves, or cross-file changes noticed but deferred.
```

## 5. Improvement Catalogue, In Priority Order

Each item names the target, the technique, the risk, and the verification. The
order is by developer value divided by risk.

### P1. Name the numbers and the slots

*Targets:* `milan/match/match.c`, `milan/match/policy.c`, `milan/core.c`,
`milan/preprocess/classification.c`, `milan/match/geometry.c`.

Every literal threshold, scale, and sentinel gets a named constant with a
comment saying what it is in domain terms and, when it is fixed-point, what
real factor it approximates (`* 246 >> 8` is 0.96; `* 291 >> 16` is 1/225).
Every slot of `metrics[77]`, `vector[51]`, `relation_values[]`, and the
`direct_metrics`/`reverse_metrics` arrays gets an `enum` index or an inline
accessor so `direct_metrics[5]` becomes something a reader can search for.
Where the RE notes do not know the vendor's name for a slot, use a descriptive
name derived from how the slot is used and say so in the comment.

*Risk:* none if the values are unchanged; the compiler emits the same code.
*Verification:* differential harness is overkill here; suites plus one
campaign are enough, and the diff should be mechanically reviewable.

### P2. Decompose the long functions

*Targets, in order:* `milan_match_prepared_probe` (`milan/match/match.c`),
`goodix_milan_profile9_build_broken_mask` and its neighbours in
`milan/preprocess/classification.c`, `goodix_milan_runtime_run`
(`milan/runtime.c`), the `*_core` functions in `milan/core.c`, and the
`*_ssm_handler`/`*_coordinator_handler` state machines in `device/`.

Extract each stage into a static function that takes exactly the data it
reads and returns exactly what it produces. Keep the call order. For the
forward/reverse duplication in `match.c`, first extract the *identical* blocks
into helpers parameterized by the metrics array and transform; leave any block
that differs by even one constant alone until the difference is understood and
commented. The state machines in `device/` are GObject/libfprint code, not
native mirrors; they can be restructured with ordinary libfprint conventions
(one function per state) and are covered by the state and runtime suites.

*Risk:* low when helpers are pure; moderate where a helper needs to mutate
several outputs and a reviewer must confirm nothing was reordered.
*Verification:* differential harness on the extracted helpers where they are
pure; full campaigns for `match.c`.

### P3. Comment intent and quirks

*Targets:* every algorithmic file; start with the ones a newcomer hits first
(`milan/core.c`, `milan/runtime.c`, `milan/match/match.c`).

A file header that says what the file produces and which native functions it
mirrors. A short comment per stage. A `Native quirk:` comment at every place
that would fail a naive code review. Comments explain *why* and *what native
does*, never restate the code. This has no code risk and can ride along with
P1 and P2 or be its own PR.

### P4. Internalize scratch buffers and tidy signatures

*Targets:* `goodix_milan_antifake_boundary_score` (`thinned`), and other
functions whose callers allocate work buffers only to pass them in. Check
`construction.c`, `core.c`, and `classification.c` for the pattern.

Allocate inside the function; drop the parameter; update `milan.h` and the
caller. This touches two or three files, so do it as its own small PR after
the file's readability PR has merged.

*Risk:* allocation failure paths change shape; make sure the failure return is
unchanged.
*Verification:* suites, one campaign.

### P5. Split `milan/milan.h`

Move the anti-fake blob and its accessors to `milan/antifake/antifake.h`, the
preprocess state to `milan/preprocess/state.h`, extraction types to
`milan/feature/`, and so on, leaving `milan.h` as an umbrella include for a
transition period. Update includes in `tests/` and
`tools/milan-parity/current/current-runner.c`.

*Risk:* build-only. *Verification:* both build modes plus the current-runner
build (a campaign run exercises it).

### P6. Isolate debug instrumentation

Replace inline `#ifdef GOODIX53X5_DEBUG` blocks with calls to hook functions
declared in one header, with no-op `static inline` definitions in release
builds and real definitions in `device/debug.c`. The algorithm files then
contain one call per observation point and no preprocessor conditionals.

*Risk:* the hook must receive exactly the same values at exactly the same
point; dumps are compared. *Verification:* build both modes; run a campaign
against a dump captured *from the new debug build* (a `capture` plus
`validate-dump` cycle), because this is the one refactor that can change
dump content.

### P7. Decide what layout mirroring to keep

Keep `_Static_assert`s on structures whose bytes cross a boundary: the
template codec, the anti-fake blob, persisted state files, debug dump records.
Remove the offset assertions on in-memory-only structures such as
`GoodixMilanPreprocessState` (the `137840`-style asserts) once the field
comments carry the native offset for anyone who needs to cross-reference
Ghidra. This is a maintainer decision; propose it in an issue before doing it.

### P8. Prefix consistency

Settle on one prefix per module (`goodix_milan_antifake_*`,
`goodix_milan_match_*`, and so on) and rename in one mechanical PR per
module, after the file-level refactors in that module have landed, so that the
rename diff is pure. Exported symbols are referenced from `tests/` and the
parity tooling; grep before renaming.

### P9. Keep profile 9 / type 12 explicit, not hard-coded

The driver supports one sensor type and one profile and should keep saying so
clearly. The improvement is to replace scattered literals such as
`chip_type == 12 ? 0 : -2` with named constants or a single
`GoodixMilanProfile` descriptor struct (thresholds, geometry, offsets) that is
filled once and passed down. This makes the profile-specific numbers visible
and searchable today. It also means that if another sensor type is ever added,
the delta is a second descriptor rather than a hunt through twelve files. Do
not build abstraction beyond that, and do not spend Ghidra time on other
profiles.

## 6. Worked Example: `milan/antifake/morphology.c`

The file computes the anti-fake boundary score. It was chosen because it is
pure (no GLib, no device state), has two exported functions and one caller
(`milan/antifake/construction.c`), and shows every pain point in 300 lines:
label sentinels `0`, `1`, `2`, `0xfd`, `0xfe`, `0xff` and thresholds `0x1a`,
`0xe7` written inline; a 130-line function containing three algorithms; the
four-neighbour index expression copied three times; no comments; a scratch
buffer in the public signature; and three native quirks that a reviewer would
be tempted to fix.

What the PR did:

- Named the labels (`ANTIFAKE_CLASS_DARK`, `..._EXCLUDED`, `..._CONFLICT`,
  `..._QUEUED`), thresholds, iteration limit, and score scale, each with a
  comment about its role in the native contract.
- Named the algorithms. The class map is a threshold classification followed
  by a priority-flood watershed; the thinning is Zhang-Suen; the score is a
  rounded ratio computed in 32-bit registers. The file header states this and
  points at the native owners (`FUN_18003d1a0`, `FUN_18003c680`,
  `FUN_18003b6d0`, `FUN_18003ad10`).
- Split `goodix_milan_antifake_class_map` into `classify_by_intensity`,
  `exclude_border`, `watershed_seed`, `watershed_inherit_label`, and
  `watershed_propagate` around a small `WatershedQueue` type whose comment
  states the two properties that determine the output (lowest level first,
  LIFO within a level).
- Split the boundary score into `boundary_maximum_residual`,
  `boundary_adjacent_differences`, and `boundary_rounded_ratio`, with the
  formula and the wrap/shift/division semantics documented on the last one.
- Marked three quirks: nonzero labels including `QUEUED`, `EXCLUDED`, and
  `CONFLICT` participate in label inheritance; thinning convergence is tested
  on the second subpass only; the maximum-residual scan uses asymmetric
  margins.
- Ran libfprint's `uncrustify` configuration over the result.

What the PR deliberately did not do:

- Change the exported signatures. The `thinned` scratch parameter is a P4
  follow-up because it touches `milan.h` and the caller.
- Remove the `if (label != ANTIFAKE_CLASS_UNASSIGNED)` branch even though it
  is provably always taken. Dead-but-native branches are removed only when the
  comment can cite the proof.
- Touch `construction.c`, the header, tests, or build lists.

Evidence recorded in the PR: 20,000 randomized differential cases at
88x108, 88x104, and random small geometries with 0 mismatches; a
`-fsanitize=address,undefined` run clean; a mutant that changed the thinning
convergence rule was caught with 110/400 mismatches; the three suites pass;
`--compare-current` passed 26/26, 454/454, and 643/643 (1,123 operations in
total) against the approved DLL on the refactored source identity.

## 7. Pitfalls Seen In Practice

- Editing a driver source while a campaign is running invalidates the run.
- Forgetting `tools/milan-parity/build-current-runner` when adding a file.
- `uncrustify` reformats `sizeof(x)` to `sizeof (x)` and re-wraps ternaries;
  run it before the differential harness, not after.
- Replacing `x < 0 ? -x : x` with `abs (x)` is fine for `int` differences of
  bytes; it is not fine for `int32_t` values that can be `INT32_MIN`.
- A helper that is "obviously equivalent" but changes the *type* of an
  intermediate (`int` vs `uint32_t`) changes wrap behaviour. The differential
  harness must feed full-range values to catch this.
- Renaming an exported symbol breaks `tests/` and the current-runner; grep
  the whole repository, not just `drivers/`.
- User-local files at the repository root (audit guides, handoffs) are not
  part of a PR. Stage files by name.

## 8. Metrics To Track

Re-run these on `drivers/goodix53x5/` after each batch of refactors. The
baseline is `main` at the time of writing.

| Metric | Baseline | Command |
| --- | --- | --- |
| Functions over 150 lines | 27 | script in section 1.2 spirit: distance between column-0 definitions |
| Functions over 300 lines | 7 | same |
| Longest function | 1,256 lines | same |
| Comment lines / total | 1,980 / 37,102 | `rg -c '^\s*(/\*\|\*\|//)'` |
| Hex literals outside `#define`/asserts | 462 | `rg -n '0x[0-9a-fA-F]{3,}' --glob '*.c' \| rg -v '#define\|_Static_assert'` |
| `#ifdef GOODIX53X5_DEBUG` sites in `match.c` | 26 | `rg -c GOODIX53X5_DEBUG milan/match/match.c` |
| Lines in `milan/milan.h` | 1,404 | `wc -l` |
| Distinct function prefixes in use | 4 major | `rg -o '^[a-z][a-z0-9_]* \('` |

Line count is intentionally absent from that table.
