// Host-side unit tests for sigfm_match_score().
//
// These do NOT need the sensor. They construct SigfmImgInfo directly with
// controlled keypoints + descriptors so the descriptor matching is 1:1 and the
// *geometry* alone decides the score. This exercises the geometric-verification
// contract of the matcher (issue #3): the score must reflect the number of
// correspondences explained by a SINGLE consistent transform, not the count of
// locally-agreeing pairs.
//
// Build:
//   g++ -std=c++17 tests/test_sigfm_match.cpp sigfm/sigfm.cpp \
//       -I/usr/include/opencv4 \
//       -lopencv_core -lopencv_features2d -lopencv_imgproc -lopencv_flann \
//       -lopencv_calib3d -o /tmp/test_sigfm && /tmp/test_sigfm

#include "../sigfm/img-info.hpp"
#include "../sigfm/sigfm.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAIL: %s\n", msg);                                        \
      failures++;                                                              \
    } else {                                                                   \
      std::printf("  ok:   %s\n", msg);                                        \
    }                                                                          \
  } while (0)

// Build an info with one-hot descriptors so frame[i] uniquely matches
// enrolled[i] (distance 0, easily passes Lowe's ratio test). Geometry is
// entirely controlled by the supplied keypoint positions.
static SigfmImgInfo *make_info(const std::vector<cv::Point2f> &pts)
{
  auto *info = new SigfmImgInfo();
  const int n = static_cast<int>(pts.size());
  info->descriptors = cv::Mat::zeros(n, n, CV_32F);
  for (int i = 0; i < n; i++)
    {
      info->keypoints.emplace_back(pts[i], 1.0f);
      info->descriptors.at<float>(i, i) = 1.0f;
    }
  return info;
}

// A rigid-ish transform (rotation + translation), the kind a genuine re-press
// of the same finger produces.
static cv::Point2f transformA(const cv::Point2f &p)
{
  const double th = 5.0 * M_PI / 180.0;
  const double c = std::cos(th), s = std::sin(th);
  return cv::Point2f(static_cast<float>(c * p.x - s * p.y + 4.0),
                     static_cast<float>(s * p.x + c * p.y + 3.0));
}

int main()
{
  // Enrolled: 40 keypoints spread across the 108x88 sensor area.
  std::vector<cv::Point2f> enr;
  for (int i = 0; i < 40; i++)
    enr.emplace_back(10.0f + (i * 37) % 88, 10.0f + (i * 53) % 68);

  // ---- Genuine: every correspondence fits ONE transform ----
  std::vector<cv::Point2f> genuine;
  for (const auto &p : enr)
    genuine.push_back(transformA(p));

  // ---- Impostor: only 6 correspondences fit transformA; the other 34 are
  // scattered (no single transform explains them) but still match by
  // descriptor. The old angle-count metric rewards the locally-consistent
  // cluster + coincidental agreements and returns a large score (false
  // accept); RANSAC should return ~6 inliers (correct reject). ----
  std::vector<cv::Point2f> impostor;
  for (int i = 0; i < 40; i++)
    {
      if (i < 6)
        impostor.push_back(transformA(enr[i]));
      else
        impostor.emplace_back(4.0f + (i * 61 + 13) % 100,
                              4.0f + (i * 29 + 7) % 80);
    }

  SigfmImgInfo *e = make_info(enr);
  SigfmImgInfo *g = make_info(genuine);
  SigfmImgInfo *imp = make_info(impostor);

  int sg = sigfm_match_score(g, e);
  int si = sigfm_match_score(imp, e);
  std::printf("genuine score = %d, impostor score = %d\n", sg, si);

  // Genuine: many correspondences fit one transform -> high score.
  CHECK(sg >= 20, "genuine (same transform) scores high");
  // Impostor: no single transform explains a meaningful share -> low score,
  // well below the per-sample counting threshold (GOODIX_SIGFM_THRESHOLD=12).
  CHECK(si < 12, "geometrically-inconsistent impostor scores low");
  // The genuine match must be clearly separated from the impostor.
  CHECK(sg - si >= 12, "genuine and impostor are clearly separated");

  sigfm_free_info(g);
  sigfm_free_info(imp);

  // ---- Degenerate: too few correspondences must reject (return 0) ----
  std::vector<cv::Point2f> few = {{10, 10}, {20, 20}, {30, 30}};
  SigfmImgInfo *e2 = make_info(few);
  SigfmImgInfo *f2 = make_info(few);
  int sf = sigfm_match_score(f2, e2);
  std::printf("too-few-keypoints score = %d\n", sf);
  CHECK(sf == 0, "too few correspondences rejects (score 0)");
  sigfm_free_info(e2);
  sigfm_free_info(f2);
  sigfm_free_info(e);

  if (failures == 0)
    {
      std::printf("\nALL TESTS PASSED\n");
      return 0;
    }
  std::printf("\n%d TEST(S) FAILED\n", failures);
  return 1;
}
