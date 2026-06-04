# RANSAC-based match scoring — fix for false-accept (issue #3)

**Date:** 2026-06-04
**Issue:** [#3 — Verification matches on non-enrolled finger](https://github.com/AndyHazz/goodix53x5-libfprint/issues/3)
**Status:** Design approved, pending implementation

## Problem

Verification matches non-enrolled fingers. Three independent reporters confirm
adjacent fingers — and even other people — pass verification. This is a
false-accept / authentication-bypass defect, the most security-critical failure
class for a biometric driver.

### Root cause

Matching is entirely host-side SIFT ("SIGFM", `sigfm/sigfm.cpp`); the sensor is
only an encrypted image source. `sigfm_match_score()` returns an **unbounded
count of pairs-of-keypoint-pairs that agree on a rotation angle** — it grows
~O(matches⁴) and is not normalized. The driver declares a match when this count
clears a fixed integer threshold (`GOODIX_SIGFM_BEST_MIN = 40`, with
`GOODIX_SIGFM_THRESHOLD = 10` over `GOODIX_SIGFM_MIN_SAMPLES = 2` of 8 samples).

A fixed threshold over an unbounded, quality-dependent metric cannot bound the
false-accept rate. The metric rewards any local ridge-orientation coherence —
which adjacent fingers on the same hand share — without requiring a single
globally-consistent geometric alignment. The codebase already documents the
problem twice: the enroll dedup check was disabled
(`goodix53x5.c:1425-1429`, "SIFT on this 108x88 sensor produces false
cross-finger matches"), and `README.md:143` records correct fingers scoring
"28–24000+" — a 3-order-of-magnitude spread with no clean separating line.

Note: the comment at `goodix53x5.c:1428` claims "Real security matching is
unaffected" — this is false; verify uses the same metric and thresholds.

## Approach

Replace the angle-pairing metric with **RANSAC geometric verification**: fit a
single rigid transform to the SIFT correspondences and count *inliers* — points
consistent with that one transform. A genuine re-press of the same finger
produces many inliers under one transform; an adjacent finger or a stranger
produces scattered matches no single transform explains, collapsing the inlier
count.

The `cv::estimateAffinePartial2D` (4-DOF: rotation + uniform scale + translation)
model is chosen deliberately over `findHomography` (8-DOF). A flat press-sensor
never produces perspective warps, so the extra DOF of a homography would let
impostor matches overfit. Constraining to physically-plausible finger placement
is itself part of the security.

The integer return contract of `sigfm_match_score` is preserved — only its
*meaning* changes (angle-count → inlier count). The C/driver API boundary is
untouched; the only behavioral surface is the threshold constants.

## Components & Changes

### 1. `sigfm/sigfm.cpp` — `sigfm_match_score()`

Keep kNN + Lowe's ratio test (current lines 141–157), which yields corresponding
point pairs. Replace the angle block (current lines 161–215) with:

1. Build `std::vector<cv::Point2f>` `src` (probe keypoints) and `dst` (enrolled
   keypoints) from the ratio-passing matches.
2. If `src.size() < MIN_MATCHES` (constant, value 8) → `return 0`.
3. `cv::Mat inliers;`
   `cv::estimateAffinePartial2D(src, dst, inliers, cv::RANSAC, RANSAC_REPROJ_THRESH);`
   with `RANSAC_REPROJ_THRESH = 3.0` (pixels).
4. If the returned transform `cv::Mat` is empty (estimation failed) → `return 0`.
5. `return cv::countNonZero(inliers);`

Preserve the existing `catch (...) { return -1; }`. **Fail closed**: any error,
empty transform, or insufficient matches rejects.

Also in this file:
- Fix `match::operator<` (lines 76–80): the second clause repeats the first
  condition and `p1.x`/`p2` are ignored, which is not a valid strict-weak
  ordering. Replace with lexicographic compare of `(p1.y, p1.x, p2.y, p2.x)`.
- Delete the now-unused `angle` struct (lines 82–90) and its constants
  (`length_match`, `angle_match` at lines 64–65). Repurpose the existing
  `min_match` constant (currently 5) as the raw-match floor `MIN_MATCHES`,
  raising it to 8; keep `distance_match` (Lowe ratio, 0.85) as-is.
- Add `#include <opencv2/calib3d.hpp>`.

### 2. `drivers/goodix53x5/goodix53x5.h` — thresholds (lines 53–56)

Reinterpret as inlier counts and add a quality gate:

```c
#define GOODIX_SIGFM_MIN_KEYPOINTS  20   /* reject low-quality probe captures */
#define GOODIX_SIGFM_THRESHOLD      12   /* min inliers for a sample to count */
#define GOODIX_SIGFM_BEST_MIN       18   /* min inliers on best-matching sample */
#define GOODIX_SIGFM_MIN_SAMPLES     2   /* min samples passing (of 8) */
```

These are conservative *starting* values to be confirmed on hardware (see
Validation). The asymmetry is intentionally safe: too-strict shows up
immediately as the enrolled finger being rejected (easily loosened), while the
structural change is what eliminates false accepts.

### 3. `drivers/goodix53x5/goodix53x5.c` — match logic (lines 1462–1616)

Add the keypoint quality gate after `keypoints = sigfm_keypoints_count(...)`
(line 1472): if `keypoints < GOODIX_SIGFM_MIN_KEYPOINTS`, report no-match
(identify) / fail (verify) and skip scoring entirely. This enforces the keypoint
count that is currently logged but ignored. The existing two-gate
`best_score`/`match_count` logic is unchanged — it now operates on inlier counts.

### 4. Build — add `opencv_calib3d`

`estimateAffinePartial2D` is in the `calib3d` module. Add `opencv_calib3d` to the
OpenCV link list in all three places that declare the others:
- `README.md` (dependency list line 31; meson snippet lines 109–114)
- `install.sh` (lines 53–58)
- `meson-integration.patch` (lines 22–28)

(The external AUR `PKGBUILD` lives outside this repo; note it in the PR so it can
be updated downstream.)

## Testing

### Host-side unit test (no sensor required)

A self-contained C++ test built against the SIGFM static library:
- Generate a synthetic textured image; create a translated+slightly-rotated copy.
- `sigfm_extract` both; assert `sigfm_match_score` > `GOODIX_SIGFM_BEST_MIN`.
- Generate an unrelated random/textured image; assert score is ~0 (well below
  `GOODIX_SIGFM_THRESHOLD`).
- Assert error/degenerate inputs (too few keypoints) return 0.

This guards the geometric logic against regression at build time. It does **not**
validate the false-accept rate on real fingerprints — that requires hardware.

### Hardware validation (user, on the Goodix sensor)

Chosen validation path: *test candidate fix, no data dump.* After building the
debug driver:
1. Enrolled finger still verifies (no false-reject regression).
2. A different finger on the same hand is **rejected**.
3. (If possible) a second person's finger is **rejected**.

Per-sample inlier counts are visible in `fp_dbg` ("verify: sample N sigfm_score
M", where M is now the inlier count). If the enrolled finger is rejected,
thresholds are too strict and get lowered; the structural fix stands regardless.

## Rollout / closing the issue

Push to a feature branch → open a PR with "Fixes #3" and the root-cause writeup
→ user validates on hardware → merge → issue auto-closes. PR #4 (the 27c6:5335
device addition) remains on hold until this merges, then can be revisited.

## Out of scope

- Recalibrating thresholds against a captured genuine-vs-impostor score
  distribution (would need a hardware data-capture loop; deferred).
- PR #4 device support.
- Issue #2 (README troubleshooting already drafted separately).
