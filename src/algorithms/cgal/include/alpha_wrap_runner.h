// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALPHA_WRAP_RUNNER_H
#define CLAW3D_ALPHA_WRAP_RUNNER_H

/// CGAL Alpha Wrap runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/alpha_wrap_contract.h"

#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API AlphaWrapRunner {
public:
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;

public:
    AlphaWrapRunner();
    ~AlphaWrapRunner();

    AlphaWrapRunner(const AlphaWrapRunner&) = delete;
    AlphaWrapRunner& operator=(const AlphaWrapRunner&) = delete;

    // Input
    void set_input_mesh(const std::vector<AW3_Point3d>& vertices,
                        const std::vector<AW3_Triangle>& faces);
    void set_input_cloud(const std::vector<AW3_Point3d>& points);

    // Run AW3.
    void run(const AW3_Config& cfg);

    // --- Live preview ---
    // Drain all pending live events into out_events. Called from main thread
    // each frame during execution. Returns true if new events were drained.
    bool drain_live_events(std::vector<AW3_FrameEvent>& out_events);
    bool drain_live_surface_snapshot(std::vector<AW3_Point3d>& verts,
                                     std::vector<AW3_Triangle>& faces);

    // Set cancel flag; worker checks between steps and stops gracefully.
    void cancel();
    bool is_cancelled() const;

    // --- Error reporting ---
    // Worker-side setter: stores an error string and pushes an Error event
    // to the live queue. Safe to call from any thread.
    void set_error(const std::string& msg);

    // Main-thread getters. has_error() is atomic; last_error() takes a mutex
    // and returns a copy. Both are safe at any time.
    bool        has_error() const;
    std::string last_error() const;
    void        clear_error();

    // --- Result ---
    void get_current_surface(std::vector<AW3_Point3d>& verts,
                             std::vector<AW3_Triangle>& faces) const;
    void get_result(std::vector<AW3_Point3d>& verts,
                    std::vector<AW3_Triangle>& faces) const;

    // --- State ---
    float progress() const;
    bool  is_done() const;
    int  current_vertex_count() const;
    int  current_face_count() const;
};

#endif // CLAW3D_ALPHA_WRAP_RUNNER_H
