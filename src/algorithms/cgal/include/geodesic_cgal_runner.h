// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_GEODESIC_CGAL_RUNNER_H
#define CLAW3D_GEODESIC_CGAL_RUNNER_H

/// CGAL geodesic runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/geodesic_contract.h"

#include <memory>
#include <string>
#include <vector>

class CLAW3D_CGAL_ALGO_API GeodesicCGALRunner {
public:
    struct Impl;

    GeodesicCGALRunner();
    ~GeodesicCGALRunner();
    GeodesicCGALRunner(const GeodesicCGALRunner&) = delete;
    GeodesicCGALRunner& operator=(const GeodesicCGALRunner&) = delete;

    void set_input(const std::vector<GEO_CGAL_Point3d>&  verts,
                   const std::vector<GEO_CGAL_Triangle>& tris);
    void set_source(const GEO_CGAL_Source& src);
    void set_target(const GEO_CGAL_Target& tgt);
    void set_sources(const std::vector<int>& source_vids);

    // Exact shortest path.
    void run(const GEO_CGAL_Config& cfg);

    // Heat Method (Direct or Intrinsic Delaunay).
    void run_heat(int variant);

    void cancel();
    bool is_cancelled() const;
    bool is_done()      const;
    bool has_error()    const;
    std::string last_error() const;

    GEO_CGAL_PathResult result() const;
    GEO_CGAL_HeatResult heat_result() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif // CLAW3D_GEODESIC_CGAL_RUNNER_H
