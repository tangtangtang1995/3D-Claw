// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "overlays/overlay_controller.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <string>
#include <vector>

void OverlayController::init_vsa_overlay(easy3d::SurfaceMesh* src) {
    clear_vsa_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.vsa;
    state.source_ghost = src;
    apply_source_wireframe_ghost(
        src,
        state.source_saved_visible,
        state.source_saved_opacity,
        state.source_saved_edge_visible,
        state.source_saved_edge_coloring_method,
        state.source_saved_edge_color,
        state.source_saved_edge_width,
        easy3d::vec4(0.65f, 0.65f, 0.65f, 1.0f));

    state.cluster_mesh = create_surface_overlay(viewer_, "vsa_clusters");
    auto src_pts = src->get_vertex_property<easy3d::vec3>("v:point");
    std::vector<easy3d::SurfaceMesh::Vertex> vmap;
    vmap.reserve(src->n_vertices());
    for (auto v : src->vertices()) {
        vmap.push_back(state.cluster_mesh->add_vertex(src_pts[v]));
    }
    for (auto f : src->faces()) {
        int idx[3]{-1, -1, -1}; int k = 0;
        for (auto v : src->vertices(f)) {
            if (k < 3) idx[k] = static_cast<int>(v.idx());
            ++k;
        }
        if (k == 3) {
            state.cluster_mesh->add_triangle(
                vmap[idx[0]], vmap[idx[1]], vmap[idx[2]]);
        }
    }
    state.cluster_mesh->add_face_property<easy3d::vec3>(
        "f:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    if (auto* fd = state.cluster_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_property_coloring(easy3d::State::FACE, "f:color");
        fd->set_opacity(1.0f);
        fd->set_lighting(false);
        fd->set_distinct_back_color(false);
        fd->update();
    }
    state.cluster_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::update_vsa_cluster_overlay(
    const std::vector<int>& face_proxy_ids)
{
    auto& state = algorithm_overlay_.vsa;
    if (!state.cluster_mesh || face_proxy_ids.empty()) return;
    if (!model_is_live(viewer_, state.cluster_mesh)) {
        state.cluster_mesh = nullptr;
        return;
    }
    auto fcolor = state.cluster_mesh->get_face_property<easy3d::vec3>("f:color");
    if (!fcolor) {
        fcolor = state.cluster_mesh->add_face_property<easy3d::vec3>(
            "f:color", easy3d::vec3(0.18f, 0.18f, 0.20f));
    }
    auto patch_id =
        state.cluster_mesh->get_face_property<int>("f:patch_id");
    if (!patch_id)
        patch_id = state.cluster_mesh->add_face_property<int>(
            "f:patch_id", -1);
    auto vsa_proxy_id =
        state.cluster_mesh->get_face_property<int>("f:vsa_proxy_id");
    if (!vsa_proxy_id)
        vsa_proxy_id = state.cluster_mesh->add_face_property<int>(
            "f:vsa_proxy_id", -1);
    const int nf = static_cast<int>(face_proxy_ids.size());
    int i = 0;
    for (auto f : state.cluster_mesh->faces()) {
        if (i >= nf) break;
        const int pid = face_proxy_ids[i++];
        fcolor[f] = (pid < 0)
                        ? easy3d::vec3(0.18f, 0.18f, 0.20f)
                        : ransac_shape_color(pid);
        patch_id[f] = pid;
        vsa_proxy_id[f] = pid;
    }
    if (auto* fd = state.cluster_mesh->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->update();
    }
    state.cluster_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::update_vsa_seed_overlay(
    const std::vector<VSA_Point3d>& seeds)
{
    if (seeds.empty()) return;
    auto& state = algorithm_overlay_.vsa;
    if (state.seed_graph && !model_is_live(viewer_, state.seed_graph))
        state.seed_graph = nullptr;
    auto* saved_current = viewer_.current_model();

    delete_model_if_live(viewer_, state.seed_graph);
    state.seed_graph = create_graph_overlay(viewer_, "vsa_seeds");
    const size_t step = std::max<size_t>(1, seeds.size() / 500 + 1);
    for (size_t i = 0; i < seeds.size(); i += step) {
        const auto& p = seeds[i];
        state.seed_graph->add_vertex(
            easy3d::vec3((float)p.x, (float)p.y, (float)p.z));
    }
    if (auto* vd = state.seed_graph->renderer()->get_points_drawable(
            "vertices", false)) {
        vd->set_uniform_coloring(easy3d::vec4(1.0f, 0.3f, 0.6f, 1.0f));
        vd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        vd->set_point_size(6.0f);
        vd->update();
    }
    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void OverlayController::clear_vsa_overlay() {
    auto& state = algorithm_overlay_.vsa;
    delete_model_if_live(viewer_, state.cluster_mesh);
    delete_model_if_live(viewer_, state.seed_graph);
    if (state.source_ghost && model_is_live(viewer_, state.source_ghost)) {
        restore_source_wireframe_ghost(
            state.source_ghost,
            state.source_saved_visible,
            state.source_saved_opacity,
            state.source_saved_edge_visible,
            state.source_saved_edge_coloring_method,
            state.source_saved_edge_color,
            state.source_saved_edge_width);
    }
    state.source_ghost = nullptr;
    viewer_.mark_dirty();
}

void OverlayController::promote_vsa_overlay_to_child(
    easy3d::SurfaceMesh* source,
    const std::string& new_name)
{
    auto& state = algorithm_overlay_.vsa;
    if (!state.cluster_mesh || !model_is_live(viewer_, state.cluster_mesh)) {
        clear_vsa_overlay();
        return;
    }
    delete_model_if_live(viewer_, state.seed_graph);

    state.cluster_mesh->set_name(new_name);
    viewer_.register_model_tree_node(state.cluster_mesh,
        ModelTreeNodeInfo{"Default", new_name,
                          source, ModelTreeNodeKind::Reconstruction, true});
    state.cluster_mesh = nullptr;
    state.source_ghost = nullptr;
    viewer_.mark_dirty();
}
