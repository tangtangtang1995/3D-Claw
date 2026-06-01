// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_VSA_RUNNER_H
#define CLAW3D_VSA_RUNNER_H

/// CGAL variational shape approximation runner behind the services facade.

#include "claw3d_cgal_algo_export.h"
#include "common/vsa_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API VSARunner {
public:
    struct Impl;

    VSARunner();
    ~VSARunner();

    VSARunner(const VSARunner&) = delete;
    VSARunner& operator=(const VSARunner&) = delete;

    void set_input(const std::vector<VSA_Point3d>&  verts,
                   const std::vector<VSA_Triangle>& tris);

    // Synchronous run. Caller spawns a worker thread if async behavior needed.
    void run(const VSA_Config& cfg);

    void cancel();
    bool is_cancelled() const;
    bool is_done()      const;
    bool has_error()    const;
    std::string last_error() const;
    void set_error(const std::string& msg);

    // Result of the most recent run() (empty if extract_mesh was disabled or
    // the extraction produced no usable geometry).
    void get_result(std::vector<VSA_Point3d>&  out_verts,
                    std::vector<VSA_Triangle>& out_tris) const;

    // Per-face proxy ids parallel to set_input() tris. -1 marks unassigned.
    // Available once run() has completed seeding + iterations.
    void get_face_proxy_ids(std::vector<int>& out) const;

    // Final proxy descriptors (seed face, error, centroid, normal). Same
    // order as proxy ids in get_face_proxy_ids().
    void get_proxies(std::vector<VSA_ProxyInfo>& out) const;

    // Latest live snapshot. Empty/zero in fast mode.
    bool poll_snapshot(int& last_generation, VSA_Snapshot& out) const;

    VSA_DebugStats debug_stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif  // CLAW3D_VSA_RUNNER_H
