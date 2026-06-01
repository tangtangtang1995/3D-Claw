// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SIMPLIFICATION_RUNNER_H
#define CLAW3D_SIMPLIFICATION_RUNNER_H

/// CGAL surface simplification runner hidden behind the services job facade.

#include "claw3d_cgal_algo_export.h"
#include "common/simplification_contract.h"

#include <memory>
#include <string>
#include <vector>

// === Public API ===

class CLAW3D_CGAL_ALGO_API SimplificationRunner {
public:
    struct Impl;

    SimplificationRunner();
    ~SimplificationRunner();

    SimplificationRunner(const SimplificationRunner&) = delete;
    SimplificationRunner& operator=(const SimplificationRunner&) = delete;

    // Set input mesh (POD triangle soup with shared vertices).
    void set_input(const std::vector<SIMPL_Point3d>&  verts,
                   const std::vector<SIMPL_Triangle>& tris);

    // Synchronous run. Caller spawns a worker thread if asynchronous behavior
    // is needed; runner only handles the algorithm + thread-safe event queue.
    void run(const SIMPL_Config& cfg);

    // Live preview
    bool drain_live_events(std::vector<SIMPL_FrameEvent>& out);

    // Snapshot poll. Returns true if a NEW snapshot is available since the
    // last call. The caller passes its last seen generation; runner fills
    // verts/tris and updates generation if newer.
    bool poll_snapshot(int& last_generation,
                       std::vector<SIMPL_Point3d>&  out_verts,
                       std::vector<SIMPL_Triangle>& out_tris) const;

    // Cancel: sets a flag the cancellable stop-predicate checks every call.
    // The current edge_collapse() call will stop as if its stop criterion was
    // satisfied; the partial result remains the simplified mesh up to that
    // moment.
    void cancel();
    bool is_cancelled() const;

    // Error reporting
    bool        has_error() const;
    std::string last_error() const;
    void        set_error(const std::string& msg);
    void        clear_error();

    // Status
    bool  is_done() const;
    float progress() const;  // 0..1, based on edges remaining vs target

    // Result (valid after is_done()).
    void get_result(std::vector<SIMPL_Point3d>&  out_verts,
                    std::vector<SIMPL_Triangle>& out_tris) const;

    SIMPL_DebugStats debug_stats() const;

private:
    std::unique_ptr<Impl> impl_;
};

#endif  // CLAW3D_SIMPLIFICATION_RUNNER_H
