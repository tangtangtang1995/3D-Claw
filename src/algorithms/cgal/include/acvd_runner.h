// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ACVD_RUNNER_H
#define CLAW3D_ACVD_RUNNER_H

/// CGAL-backed ACVD runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/acvd_contract.h"

#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API ACVDRunner {
public:
    struct Impl;

    ACVDRunner();
    ~ACVDRunner();

    ACVDRunner(const ACVDRunner&) = delete;
    ACVDRunner& operator=(const ACVDRunner&) = delete;

    // Set input mesh (triangle soup with shared vertices).
    void set_input(const std::vector<ACVD_Point3d>&  verts,
                   const std::vector<ACVD_Triangle>& tris);

    // Synchronous run. Caller spawns a worker thread if async behavior needed.
    void run(const ACVD_Config& cfg);

    // Live preview.
    bool drain_live_events(std::vector<ACVD_FrameEvent>& out);

    // Cluster snapshot (face_cluster_ids per face). The overlay uses this to
    // build a colored overlay mesh.
    bool poll_cluster_snapshot(int& last_generation,
                               std::vector<ACVD_Point3d>&  verts,
                               std::vector<ACVD_Triangle>& tris,
                               std::vector<int>&            face_cluster_ids,
                               std::vector<ACVD_Point3d>&  cluster_centers) const;

    // Initial seed positions, captured by on_seed_created() at the start of
    // the clustering loop. Stable across the whole run, unlike snapshot
    // cluster_centers which jitter as vertices reassign. Used by the UI to
    // show "where the seeds appeared". May return an empty vector before
    // seeding has happened.
    void get_seed_positions(std::vector<ACVD_Point3d>& out) const;

    // Cancel: sets a flag. Without a visitor the cancel is not
    // responsive mid-iteration on large meshes; the visitor adds
    // cooperative checking via go_further().
    void cancel();
    bool is_cancelled() const;

    // Error reporting.
    bool        has_error() const;
    std::string last_error() const;
    void        set_error(const std::string& msg);
    void        clear_error();

    // Status.
    bool  is_done() const;
    // Coarse progress: 0.0 = not started, 1.0 = done.

    // Result (valid after is_done()).
    void get_result(std::vector<ACVD_Point3d>&  out_verts,
                    std::vector<ACVD_Triangle>& out_tris) const;

    ACVD_DebugStats debug_stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif  // CLAW3D_ACVD_RUNNER_H
