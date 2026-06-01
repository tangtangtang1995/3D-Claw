// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PLANAR_PATCH_REMESHING_RUNNER_H
#define CLAW3D_PLANAR_PATCH_REMESHING_RUNNER_H

/// CGAL planar patch remeshing runner behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/planar_patch_remeshing_contract.h"

#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API PlanarPatchRemeshingRunner {
public:
    struct Impl;
    PlanarPatchRemeshingRunner();
    ~PlanarPatchRemeshingRunner();
    PlanarPatchRemeshingRunner(const PlanarPatchRemeshingRunner&) = delete;
    PlanarPatchRemeshingRunner& operator=(const PlanarPatchRemeshingRunner&) = delete;

    void set_input(const std::vector<PPR_Point3d>& verts,
                   const std::vector<PPR_Triangle>& tris);
    void set_face_patch_ids(const std::vector<int>& face_patch_ids);
    void run(const PPR_Config& cfg);

    void cancel();
    bool is_cancelled() const;
    bool is_done() const;
    void set_error(const std::string& msg);
    bool has_error() const;
    std::string last_error() const;

    bool poll_snapshot(int& last_generation, PPR_Snapshot& out) const;
    bool has_pending_snapshot(int last_generation) const;
    void get_result(std::vector<PPR_Point3d>& verts,
                    std::vector<PPR_Triangle>& tris,
                    std::vector<int>& face_patch_ids) const;
    PPR_DebugStats debug_stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_PLANAR_PATCH_REMESHING_RUNNER_H
