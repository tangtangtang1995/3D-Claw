// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PARAMETERIZATION_RUNNER_H
#define CLAW3D_PARAMETERIZATION_RUNNER_H

/// CGAL parameterization runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/parameterization_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API ParameterizationRunner {
public:
    struct Impl;

    ParameterizationRunner();
    ~ParameterizationRunner();

    ParameterizationRunner(const ParameterizationRunner&) = delete;
    ParameterizationRunner& operator=(const ParameterizationRunner&) = delete;

    void set_input(const std::vector<PARAM_Point3d>&  verts,
                   const std::vector<PARAM_Triangle>& tris);

    // Algorithm config.
    void set_config(const PARAM_Config& cfg);
    void set_wake_callback(PARAM_WakeCallback cb, void* user_data);

    // Optional: set seam paths for closed meshes.
    void set_seams(const std::vector<PARAM_SeamPath>& seams);
    void clear_seams();

    void run();
    void cancel();
    void fail_with_exception(const char* message);
    bool is_done()      const;
    bool is_cancelled() const;
    bool has_error()    const;
    std::string last_error() const;

    void get_result(PARAM_Result& out) const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_PARAMETERIZATION_RUNNER_H
