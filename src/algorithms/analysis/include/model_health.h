// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MODEL_HEALTH_H
#define CLAW3D_MODEL_HEALTH_H

/// Model-health checks for mesh topology, geometry, and component summaries.

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace easy3d { class Model; }

// "Health" = detailed model diagnostics computed lazily when the Health
// Report panel is shown or when AI Chat explicitly requests it.
struct ModelHealth {
    enum State { NotComputed, Computing, Ready, Failed };
    std::atomic<int> state{ NotComputed };

    // Surface mesh
    int boundary_edges = 0;
    int boundary_loops = 0;
    int degenerate_faces = 0;
    int isolated_vertices = 0;
    int non_manifold_vertices = 0;
    int connected_components = 0;

    // Common attribute availability
    bool has_vertex_normals = false;
    bool has_vertex_colors  = false;
    bool has_face_colors    = false;
    bool has_uv             = false;
    int  texture_paths      = 0;     // number of texture filenames declared

    // Memory (rough)
    float estimated_memory_mb = 0.0f;

    // Diagnostics
    float compute_time_sec = 0.0f;
    std::string error_message;        // populated when state == Failed
};

class ModelHealthRegistry {
public:
    static ModelHealthRegistry& instance();

    // Returns the cached entry (may be in any state). Never null while the
    // model is alive in the registry.
    const ModelHealth* get(easy3d::Model* m) const;

    // Triggers a compute if not already done. Safe to call repeatedly.
    void request_compute(easy3d::Model* m);

    // Drop cache for model (e.g., on delete / clear_scene / geometry edit).
    void invalidate(easy3d::Model* m);
    void clear();

    // Format the cached health as a compact block for AI context. Returns
    // empty when no entry / not yet ready.
    std::string format_for_ai(easy3d::Model* m) const;

private:
    ModelHealthRegistry() = default;
    ModelHealthRegistry(const ModelHealthRegistry&) = delete;
    ModelHealthRegistry& operator=(const ModelHealthRegistry&) = delete;

    mutable std::mutex mu_;
    // unique_ptr because ModelHealth has an atomic member -> not copyable.
    std::unordered_map<easy3d::Model*, std::unique_ptr<ModelHealth>> cache_;
};

#endif // CLAW3D_MODEL_HEALTH_H
