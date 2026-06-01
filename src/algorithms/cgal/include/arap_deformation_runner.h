// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ARAP_DEFORMATION_RUNNER_H
#define CLAW3D_ARAP_DEFORMATION_RUNNER_H

/// CGAL ARAP deformation runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/arap_deformation_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API ARAPDeformationRunner {
public:
    struct Impl;

    ARAPDeformationRunner();
    ~ARAPDeformationRunner();

    ARAPDeformationRunner(const ARAPDeformationRunner&)            = delete;
    ARAPDeformationRunner& operator=(const ARAPDeformationRunner&) = delete;

    void set_input(const std::vector<ARAP_Point3d>&  verts,
                   const std::vector<ARAP_Triangle>& tris);
    void set_selection(const ARAP_Selection& selection);
    void set_control_transforms(
        const std::vector<ARAP_ControlTransform>& transforms);

    // Synchronous: validate -> build CGAL mesh -> preprocess -> apply
    // transforms -> deform -> fill MCF_Result. Caller spawns a worker
    // thread for async behavior.
    void run(const ARAP_Config& cfg);

    void cancel();
    bool is_cancelled() const;
    bool is_done()      const;
    bool has_error()    const;
    std::string last_error() const;
    void set_error(ARAP_ErrorCode code, const std::string& msg);

    bool poll_snapshot(int& last_generation, ARAP_Snapshot& out) const;

    void get_result(ARAP_Result& out) const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_ARAP_DEFORMATION_RUNNER_H
