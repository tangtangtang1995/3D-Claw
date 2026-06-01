// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#pragma once

/// Helpers for creating, styling, and safely deleting temporary overlay models.

#include <easy3d/core/types.h>

#include <string>

namespace easy3d {
    class SurfaceMesh;
    class Graph;
    class Model;
}
class ViewportCanvas;

// Create a fresh easy3d::Graph (or SurfaceMesh), name it, register it as
// an overlay node under the "_overlays" workspace, and add it to the
// viewer. Caller still owns the returned pointer through the viewer
// (delete via `delete_model_if_live` when done).
//
// `visible_in_tree` defaults to false so overlays don't pollute the
// Model List the user sees. The 4 inline candidate sites that this
// helper replaces all used `visible_in_tree=false`; pass true only if
// you specifically want the overlay listed.
easy3d::Graph* create_graph_overlay(ViewportCanvas& viewer,
                                    const std::string& name,
                                    bool visible_in_tree = false);

easy3d::SurfaceMesh* create_surface_overlay(ViewportCanvas& viewer,
                                            const std::string& name,
                                            bool visible_in_tree = false);

// Returns true iff `model` is still in `viewer.models()`. Used by every
// algorithm-overlay file to guard against pointers to overlays the user
// already deleted from the Model List (the overlay caches the raw pointer
// but the actual model is owned by the viewer's shared_ptr vector). The
// 7 overlay files used to ship byte-identical inline lambdas for this --
// this is the unified version.
bool model_is_live(ViewportCanvas& viewer, easy3d::Model* model);

namespace claw_overlay_detail {
// Non-template impl: viewer.delete_model() needs the full
// ViewportCanvas type, which we don't want to drag into the header
// just for this helper. The template wrapper below downcasts and
// forwards through this thunk so we only need a forward declaration.
void delete_live_model_base(ViewportCanvas& viewer, easy3d::Model* model);
}

// If `model` is non-null and still live in `viewer`, delete it and clear
// the pointer to null. Returns true iff a model was actually deleted.
// Safe to call when the pointer is null or stale: it just clears it.
//
// Template so callers can pass `Graph*&` / `SurfaceMesh*&` / `PointCloud*&`
// directly without manually casting through `easy3d::Model*&` (a `Graph*&`
// is NOT implicitly convertible to a `Model*&` even though `Graph*` is to
// `Model*` -- references would otherwise let derived = derived_other).
template <typename ModelT>
bool delete_model_if_live(ViewportCanvas& viewer, ModelT*& model) {
    easy3d::Model* base = model;
    if (!model_is_live(viewer, base)) {
        model = nullptr;
        return false;
    }
    claw_overlay_detail::delete_live_model_base(viewer, base);
    model = nullptr;
    return true;
}

// Shared helpers for temporary overlay visualization. These helpers only touch
// renderer/drawable style and deliberately do not own model lifetime.
void apply_source_wireframe_ghost(
    easy3d::SurfaceMesh* mesh,
    bool& saved_renderer_visible,
    float& saved_face_opacity,
    bool& saved_edge_visible,
    int& saved_edge_coloring_method,
    easy3d::vec4& saved_edge_color,
    float& saved_edge_width,
    const easy3d::vec4& ghost_edge_color = easy3d::vec4(0.75f, 0.75f, 0.75f, 1.0f),
    float ghost_edge_width = 1.0f);

void restore_source_wireframe_ghost(
    easy3d::SurfaceMesh* mesh,
    bool saved_renderer_visible,
    float saved_face_opacity,
    bool saved_edge_visible,
    int saved_edge_coloring_method,
    const easy3d::vec4& saved_edge_color,
    float saved_edge_width);
