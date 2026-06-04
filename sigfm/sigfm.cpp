// SIGFM algorithm for libfprint

// Copyright (C) 2022 Matthieu CHARETTE <matthieu.charette@gmail.com>
// Copyright (c) 2022 Natasha England-Elbro <ashenglandelbro@protonmail.com>
// Copyright (c) 2022 Timur Mangliev <tigrmango@gmail.com>

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
//

#include "sigfm.hpp"
#include "binary.hpp"
#include "img-info.hpp"

#include "opencv2/calib3d.hpp"
#include "opencv2/core/persistence.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/features2d.hpp"
#include "opencv2/imgcodecs.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <opencv2/opencv.hpp>
#include <vector>

namespace bin {

template<>
struct serializer<SigfmImgInfo> : public std::true_type {
    static void serialize(const SigfmImgInfo& info, stream& out)
    {
        out << info.keypoints << info.descriptors;
    }
};

template<>
struct deserializer<SigfmImgInfo> : public std::true_type {
    static SigfmImgInfo deserialize(stream& in)
    {
        SigfmImgInfo info;
        in >> info.keypoints >> info.descriptors;
        return info;
    }
};
} // namespace bin

namespace {
constexpr auto distance_match = 0.85;
constexpr auto min_match = 8;       // min raw descriptor matches before RANSAC
constexpr auto ransac_reproj_thresh = 3.0; // px: inlier tolerance for the fit
struct match {
    cv::Point2i p1;
    cv::Point2i p2;
    match(cv::Point2i ip1, cv::Point2i ip2) : p1{ip1}, p2{ip2} {}
    match() : p1{cv::Point2i(0, 0)}, p2{cv::Point2i(0, 0)} {}
    bool operator==(const match& right) const
    {
        return std::tie(this->p1, this->p2) == std::tie(right.p1, right.p2);
    }
    bool operator<(const match& right) const
    {
        return std::tie(p1.y, p1.x, p2.y, p2.x) <
               std::tie(right.p1.y, right.p1.x, right.p2.y, right.p2.x);
    }
};
} // namespace

SigfmImgInfo* sigfm_copy_info(SigfmImgInfo* info) { return new SigfmImgInfo{*info}; }

int sigfm_keypoints_count(SigfmImgInfo* info) { return info->keypoints.size(); }
unsigned char* sigfm_serialize_binary(SigfmImgInfo* info, int* outlen)
{
    bin::stream s;
    s << *info;
    *outlen = s.size();
    return s.copy_buffer();
}

SigfmImgInfo* sigfm_deserialize_binary(const unsigned char* bytes, int len)
{
    try {
        bin::stream s{bytes, bytes + len};
        auto info = std::make_unique<SigfmImgInfo>();
        s >> *info;
        return info.release();
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

SigfmImgInfo* sigfm_extract(const SigfmPix* pix, int width, int height)
{
    cv::Mat img;
    img.create(height, width, CV_8UC1);
    std::memcpy(img.data, pix, width * height);

    /* Apply CLAHE to enhance local contrast for better SIFT detection */
    auto clahe = cv::createCLAHE(4.0, cv::Size(4, 4));
    cv::Mat enhanced;
    clahe->apply(img, enhanced);

    const auto roi = cv::Mat::ones(cv::Size{enhanced.size[1], enhanced.size[0]}, CV_8UC1);
    std::vector<cv::KeyPoint> pts;

    cv::Mat descs;
    cv::SIFT::create()->detectAndCompute(enhanced, roi, pts, descs);

    auto* info = new SigfmImgInfo{pts, descs};
    return info;
}

int sigfm_match_score(SigfmImgInfo* frame, SigfmImgInfo* enrolled)
{
    try {
        std::vector<std::vector<cv::DMatch>> points;
        auto bfm = cv::BFMatcher::create();
        bfm->knnMatch(frame->descriptors, enrolled->descriptors, points, 2);
        std::set<match> matches_unique;
        int nb_matched = 0;
        for (const auto& pts : points) {
            if (pts.size() < 2) {
                continue;
            }
            const cv::DMatch& match_1 = pts.at(0);
            if (match_1.distance < distance_match * pts.at(1).distance) {
                matches_unique.emplace(
                    match{frame->keypoints.at(match_1.queryIdx).pt,
                          enrolled->keypoints.at(match_1.trainIdx).pt});
                nb_matched++;
            }
        }
        if (nb_matched < min_match) {
            return 0;
        }
        std::vector<match> matches{matches_unique.begin(),
                                   matches_unique.end()};
        if (matches.size() < static_cast<std::size_t>(min_match)) {
            return 0;
        }

        // Robust geometric verification. Estimate a SINGLE rigid+uniform-scale
        // transform (rotation, scale, translation -- 4 DOF) between the matched
        // keypoints and count its inliers. A genuine re-press of the same
        // finger yields many inliers under one transform; a different finger
        // (or a different person) yields scattered matches that no single
        // transform explains, collapsing the inlier count. The inlier count IS
        // the score, bounded by the number of matches. A partial-affine model
        // is used deliberately over a homography: a flat press-sensor produces
        // no perspective warp, so the extra DOF would only let impostor matches
        // overfit.
        std::vector<cv::Point2f> src;
        std::vector<cv::Point2f> dst;
        src.reserve(matches.size());
        dst.reserve(matches.size());
        for (const auto& m : matches) {
            src.emplace_back(static_cast<float>(m.p1.x),
                             static_cast<float>(m.p1.y));
            dst.emplace_back(static_cast<float>(m.p2.x),
                             static_cast<float>(m.p2.y));
        }

        cv::Mat inliers;
        cv::Mat transform = cv::estimateAffinePartial2D(
            src, dst, inliers, cv::RANSAC, ransac_reproj_thresh);
        if (transform.empty()) {
            return 0;
        }
        return cv::countNonZero(inliers);
    }
    catch (...) {
        return -1;
    }
}

void sigfm_free_info(SigfmImgInfo* info) { delete info; }
