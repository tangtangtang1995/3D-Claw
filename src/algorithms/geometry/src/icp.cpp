// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "icp.h"

#include <easy3d/core/matrix_algo.h>
#include <easy3d/kdtree/kdtree_search_nanoflann.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace claw3d::algo {

bool kabsch_rigid(const std::vector<easy3d::vec3>& src,
                  const std::vector<easy3d::vec3>& dst,
                  easy3d::mat4& out_T,
                  float& out_rms)
{
    const std::size_t n = src.size();
    if (n < 3 || dst.size() != n)
        return false;

    easy3d::dvec3 cs(0, 0, 0), cd(0, 0, 0);
    for (std::size_t i = 0; i < n; ++i) {
        cs += easy3d::dvec3(src[i].x, src[i].y, src[i].z);
        cd += easy3d::dvec3(dst[i].x, dst[i].y, dst[i].z);
    }
    cs /= double(n);
    cd /= double(n);

    easy3d::Matrix<double> H(3, 3, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        easy3d::dvec3 p(src[i].x - cs.x, src[i].y - cs.y, src[i].z - cs.z);
        easy3d::dvec3 q(dst[i].x - cd.x, dst[i].y - cd.y, dst[i].z - cd.z);
        H(0, 0) += p.x * q.x; H(0, 1) += p.x * q.y; H(0, 2) += p.x * q.z;
        H(1, 0) += p.y * q.x; H(1, 1) += p.y * q.y; H(1, 2) += p.y * q.z;
        H(2, 0) += p.z * q.x; H(2, 1) += p.z * q.y; H(2, 2) += p.z * q.z;
    }

    easy3d::Matrix<double> U(3, 3, 0.0), S(3, 3, 0.0), V(3, 3, 0.0);
    easy3d::svd_decompose(H, U, S, V);

    easy3d::Matrix<double> VUt(3, 3, 0.0);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                VUt(i, j) += V(i, k) * U(j, k);

    double det = VUt(0, 0) * (VUt(1, 1) * VUt(2, 2) - VUt(1, 2) * VUt(2, 1))
               - VUt(0, 1) * (VUt(1, 0) * VUt(2, 2) - VUt(1, 2) * VUt(2, 0))
               + VUt(0, 2) * (VUt(1, 0) * VUt(2, 1) - VUt(1, 1) * VUt(2, 0));
    double sgn = (det >= 0.0) ? 1.0 : -1.0;

    easy3d::Matrix<double> R(3, 3, 0.0);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R(i, j) = V(i, 0) * U(j, 0)
                    + V(i, 1) * U(j, 1)
                    + V(i, 2) * U(j, 2) * sgn;
        }
    }

    easy3d::dvec3 t;
    t.x = cd.x - (R(0, 0) * cs.x + R(0, 1) * cs.y + R(0, 2) * cs.z);
    t.y = cd.y - (R(1, 0) * cs.x + R(1, 1) * cs.y + R(1, 2) * cs.z);
    t.z = cd.z - (R(2, 0) * cs.x + R(2, 1) * cs.y + R(2, 2) * cs.z);

    out_T = easy3d::mat4::identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out_T(i, j) = float(R(i, j));
    out_T(0, 3) = float(t.x);
    out_T(1, 3) = float(t.y);
    out_T(2, 3) = float(t.z);

    double err2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double x = R(0, 0) * src[i].x + R(0, 1) * src[i].y
                 + R(0, 2) * src[i].z + t.x - dst[i].x;
        double y = R(1, 0) * src[i].x + R(1, 1) * src[i].y
                 + R(1, 2) * src[i].z + t.y - dst[i].y;
        double z = R(2, 0) * src[i].x + R(2, 1) * src[i].y
                 + R(2, 2) * src[i].z + t.z - dst[i].z;
        err2 += x * x + y * y + z * z;
    }
    out_rms = float(std::sqrt(err2 / double(n)));
    return true;
}

ICPResult run_icp(const std::vector<easy3d::vec3>& src_full,
                  const std::vector<easy3d::vec3>& dst_full,
                  int max_iter,
                  int sample_count,
                  float outlier_factor,
                  float tolerance)
{
    ICPResult res;
    if (src_full.size() < 3 || dst_full.size() < 3)
        return res;

    std::vector<easy3d::vec3> src;
    if (sample_count > 0 && static_cast<int>(src_full.size()) > sample_count) {
        std::mt19937 rng(1337);
        std::uniform_int_distribution<int> u(0, static_cast<int>(src_full.size()) - 1);
        src.reserve(sample_count);
        for (int i = 0; i < sample_count; ++i)
            src.push_back(src_full[u(rng)]);
    } else {
        src = src_full;
    }

    easy3d::KdTreeSearch_NanoFLANN tree(dst_full);
    float prev_rms = std::numeric_limits<float>::infinity();
    easy3d::mat4 T = easy3d::mat4::identity();

    std::vector<easy3d::vec3> cur(src.size());
    std::vector<easy3d::vec3> matched(src.size());

    for (int iter = 0; iter < max_iter; ++iter) {
        for (std::size_t i = 0; i < src.size(); ++i) {
            const easy3d::vec3& p = src[i];
            cur[i] = easy3d::vec3(
                T(0, 0) * p.x + T(0, 1) * p.y + T(0, 2) * p.z + T(0, 3),
                T(1, 0) * p.x + T(1, 1) * p.y + T(1, 2) * p.z + T(1, 3),
                T(2, 0) * p.x + T(2, 1) * p.y + T(2, 2) * p.z + T(2, 3));
        }

        std::vector<float> dists(src.size());
        for (std::size_t i = 0; i < cur.size(); ++i) {
            float sq = 0.0f;
            int idx = tree.find_closest_point(cur[i], sq);
            if (idx < 0 || idx >= static_cast<int>(dst_full.size())) {
                dists[i] = std::numeric_limits<float>::infinity();
                matched[i] = cur[i];
            } else {
                matched[i] = dst_full[idx];
                dists[i] = std::sqrt(sq);
            }
        }

        std::vector<float> sorted_d = dists;
        std::nth_element(sorted_d.begin(),
                         sorted_d.begin() + sorted_d.size() / 2,
                         sorted_d.end());
        float median = sorted_d[sorted_d.size() / 2];
        float thresh = std::max(1e-6f, outlier_factor * median);

        std::vector<easy3d::vec3> good_src, good_dst;
        good_src.reserve(src.size());
        good_dst.reserve(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) {
            if (dists[i] <= thresh) {
                good_src.push_back(src[i]);
                good_dst.push_back(matched[i]);
            }
        }
        if (good_src.size() < 3) {
            res.ok = false;
            return res;
        }

        easy3d::mat4 T_iter;
        float iter_rms = 0.0f;
        if (!kabsch_rigid(good_src, good_dst, T_iter, iter_rms))
            return res;
        T = T_iter;
        res.correspondences = static_cast<int>(good_src.size());
        res.iterations_run = iter + 1;
        res.rms = iter_rms;

        if (std::abs(prev_rms - iter_rms) < tolerance)
            break;
        prev_rms = iter_rms;
    }

    res.T = T;
    res.ok = true;
    return res;
}

} // namespace claw3d::algo
