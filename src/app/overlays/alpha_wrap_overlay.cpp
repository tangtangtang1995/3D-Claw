// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Alpha Wrap 3D live-preview overlays.

#include "overlays/overlay_controller.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include "common/alpha_wrap_contract.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/types.h>
#include <easy3d/core/surface_mesh_builder.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <vector>

namespace {

easy3d::SurfaceMesh* convert_aw3_pod_to_easy3d(
        const std::vector<AW3_Point3d>& points,
        const std::vector<AW3_Triangle>& faces) {
    auto* mesh = new easy3d::SurfaceMesh;
    easy3d::SurfaceMeshBuilder builder(mesh);
    builder.begin_surface();

    std::vector<easy3d::SurfaceMesh::Vertex> vertices;
    vertices.reserve(points.size());
    for (const auto& p : points) {
        vertices.push_back(builder.add_vertex(easy3d::vec3(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z))));
    }

    for (const auto& f : faces) {
        if (f.v0 < 0 || f.v1 < 0 || f.v2 < 0 ||
            f.v0 >= static_cast<int>(vertices.size()) ||
            f.v1 >= static_cast<int>(vertices.size()) ||
            f.v2 >= static_cast<int>(vertices.size())) {
            continue;
        }
        builder.add_face({vertices[f.v0], vertices[f.v1], vertices[f.v2]});
    }

    builder.end_surface(false);
    return mesh;
}

} // namespace

void OverlayController::reset_aw3_process_overlay() {
    auto& aw3 = algorithm_overlay_.aw3;

    auto* saved_current = viewer_.current_model();

    if (aw3.live_surface_mesh) {
        if (model_is_live(viewer_, aw3.live_surface_mesh))
            viewer_.delete_model(aw3.live_surface_mesh);
        aw3.live_surface_mesh = nullptr;
    }
    if (aw3.gate_mesh) {
        if (model_is_live(viewer_, aw3.gate_mesh))
            viewer_.delete_model(aw3.gate_mesh);
        aw3.gate_mesh = nullptr;
    }
    if (aw3.steiner_cloud) {
        if (model_is_live(viewer_, aw3.steiner_cloud))
            viewer_.delete_model(aw3.steiner_cloud);
        aw3.steiner_cloud = nullptr;
    }

    aw3.steiner_events.clear();
    aw3.gate_events.clear();
    aw3.has_gate = false;

    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void OverlayController::clear_aw3_live_surface_overlay() {
    auto& aw3 = algorithm_overlay_.aw3;

    auto* saved_current = viewer_.current_model();
    auto* removed = aw3.live_surface_mesh;
    if (aw3.live_surface_mesh) {
        if (model_is_live(viewer_, aw3.live_surface_mesh))
            viewer_.delete_model(aw3.live_surface_mesh);
        aw3.live_surface_mesh = nullptr;
    }

    if (saved_current && saved_current != removed && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void OverlayController::refresh_aw3_live_surface_style(const Aw3OverlayOptions& options) {
    auto& aw3 = algorithm_overlay_.aw3;

    if (aw3.live_surface_mesh && !model_is_live(viewer_, aw3.live_surface_mesh))
        aw3.live_surface_mesh = nullptr;
    if (!aw3.live_surface_mesh)
        return;

    float opacity = options.live_surface_opacity;
    if (opacity < 0.05f) opacity = 0.05f;
    if (opacity > 0.80f) opacity = 0.80f;

    auto* fd = aw3.live_surface_mesh->renderer()->get_triangles_drawable("faces", false);
    if (fd) {
        fd->set_uniform_coloring(easy3d::vec4(0.05f, 0.65f, 1.0f, 1.0f));
        fd->set_opacity(opacity);
        fd->set_smooth_shading(false);
    }
    auto* ed = aw3.live_surface_mesh->renderer()->get_lines_drawable("edges", false);
    if (ed) {
        ed->set_visible(options.live_surface_wireframe);
        ed->set_uniform_coloring(easy3d::vec4(0.0f, 0.9f, 1.0f, 1.0f));
        ed->set_line_width(1.0f);
        ed->set_impostor_type(easy3d::LinesDrawable::PLAIN);
    }
    auto* vd = aw3.live_surface_mesh->renderer()->get_points_drawable("vertices", false);
    if (vd)
        vd->set_visible(false);
    aw3.live_surface_mesh->renderer()->update();
    viewer_.mark_dirty();
}

void OverlayController::update_aw3_live_surface_overlay(
    const std::vector<AW3_Point3d>& verts,
    const std::vector<AW3_Triangle>& faces,
    const Aw3OverlayOptions& options)
{
    auto& aw3 = algorithm_overlay_.aw3;

    if (verts.empty() || faces.empty()) {
        clear_aw3_live_surface_overlay();
        return;
    }

    auto* saved_current = viewer_.current_model();
    auto* removed = aw3.live_surface_mesh;
    if (aw3.live_surface_mesh) {
        if (model_is_live(viewer_, aw3.live_surface_mesh))
            viewer_.delete_model(aw3.live_surface_mesh);
        aw3.live_surface_mesh = nullptr;
    }

    aw3.live_surface_mesh = convert_aw3_pod_to_easy3d(verts, faces);
    if (!aw3.live_surface_mesh) {
        if (saved_current && saved_current != removed && model_is_live(viewer_, saved_current))
            viewer_.set_current_model(saved_current);
        viewer_.mark_dirty();
        return;
    }

    aw3.live_surface_mesh->set_name("aw3_live_surface");
    viewer_.add_model(aw3.live_surface_mesh);
    viewer_.register_model_tree_overlay(aw3.live_surface_mesh, saved_current);
    refresh_aw3_live_surface_style(options);

    if (saved_current && saved_current != removed && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}

void OverlayController::update_aw3_live_overlay(const std::vector<AW3_FrameEvent>& events, const Aw3OverlayOptions& options, bool force) {
    auto& aw3 = algorithm_overlay_.aw3;
    size_t added = 0;
    bool gate_changed = false;
    for (const auto& ev : events) {
        if (ev.type == AW3_FrameEvent::SteinerR1 ||
            ev.type == AW3_FrameEvent::SteinerR2) {
            aw3.steiner_events.push_back(ev);
            ++added;
        } else if (ev.type == AW3_FrameEvent::Gate) {
            aw3.gate_event = ev;
            aw3.gate_events.push_back(ev);
            aw3.has_gate = true;
            gate_changed = true;
        }
    }
    if (added == 0 && !gate_changed && !force)
        return;


    if (aw3.steiner_cloud && !model_is_live(viewer_, aw3.steiner_cloud))
        aw3.steiner_cloud = nullptr;
    if (aw3.gate_mesh && !model_is_live(viewer_, aw3.gate_mesh))
        aw3.gate_mesh = nullptr;
    if (aw3.gate_events.size() > 200)
        aw3.gate_events.erase(
            aw3.gate_events.begin(),
            aw3.gate_events.end() - 200);
    // Guardrail: cap the live Steiner buffer regardless of how
    // much the DLL pushes. Keeps GUI memory bounded for huge runs.
    constexpr size_t MAX_LIVE_STEINER = 60000;
    if (aw3.steiner_events.size() > MAX_LIVE_STEINER) {
        aw3.steiner_events.erase(
            aw3.steiner_events.begin(),
            aw3.steiner_events.end() - MAX_LIVE_STEINER);
    }

    auto* saved_current = viewer_.current_model();

    if (!aw3.steiner_events.empty() && (added > 0 || force)) {
        bool created = false;
        if (!aw3.steiner_cloud) {
            aw3.steiner_cloud = new easy3d::PointCloud;
            aw3.steiner_cloud->set_name("aw3_live_steiner");
            aw3.steiner_cloud->add_vertex_property<easy3d::vec3>(
                "v:color", easy3d::vec3(0.2f, 1.0f, 0.3f));
            created = true;
        }

        int mode = options.live_display_mode;
        if (mode < 0) mode = 0;
        if (mode > 2) mode = 2;
        int recent_count = std::max(1, options.live_recent_count);

        const size_t total = aw3.steiner_events.size();
        size_t first = 0;
        if (mode == 2 && total > (size_t)recent_count)
            first = total - (size_t)recent_count;
        const size_t display_count = total - first;

        aw3.steiner_cloud->resize((unsigned int)display_count);
        auto& pts = aw3.steiner_cloud->get_vertex_property<easy3d::vec3>("v:point").vector();
        auto colors = aw3.steiner_cloud->vertex_property<easy3d::vec3>(
            "v:color", easy3d::vec3(0.2f, 1.0f, 0.3f));
        auto& color_vec = colors.vector();

        auto color_for = [mode, recent_count, total](const AW3_FrameEvent& ev, size_t src_idx) {
            const bool r2 = (ev.type == AW3_FrameEvent::SteinerR2);
            const bool recent = (total - 1 - src_idx) < (size_t)recent_count;
            if (mode == 1 && !recent)
                return r2 ? easy3d::vec3(0.42f, 0.34f, 0.10f)
                          : easy3d::vec3(0.10f, 0.32f, 0.20f);
            return r2 ? easy3d::vec3(1.0f, 0.85f, 0.0f)
                      : easy3d::vec3(0.2f, 1.0f, 0.3f);
        };

        for (size_t j = 0; j < display_count; ++j) {
            const size_t i = first + j;
            const auto& ev = aw3.steiner_events[i];
            pts[j] = easy3d::vec3((float)ev.point[0], (float)ev.point[1], (float)ev.point[2]);
            color_vec[j] = color_for(ev, i);
        }

        if (created) {
            viewer_.add_model(aw3.steiner_cloud);
            viewer_.register_model_tree_overlay(aw3.steiner_cloud, saved_current);
        }

        auto* sd = aw3.steiner_cloud->renderer()->get_points_drawable("vertices", false);
        if (sd) {
            sd->set_property_coloring(easy3d::State::VERTEX, "v:color");
            sd->set_impostor_type(easy3d::PointsDrawable::SPHERE);
            sd->set_point_size(7.0f);
            sd->update();
        } else {
            aw3.steiner_cloud->renderer()->update();
        }
    }

    if (!options.live_show_gate) {
        if (aw3.gate_mesh) {
            viewer_.delete_model(aw3.gate_mesh);
            aw3.gate_mesh = nullptr;
        }
    } else if (!aw3.gate_events.empty() && (gate_changed || force)) {
        bool created = false;
        if (!aw3.gate_mesh) {
            aw3.gate_mesh = new easy3d::SurfaceMesh;
            aw3.gate_mesh->set_name("aw3_live_gate_trail");
            created = true;
        } else {
            aw3.gate_mesh->clear();
        }

        int trail_count = std::max(1, options.live_gate_trail_count);
        const size_t total = aw3.gate_events.size();
        size_t first = 0;
        if (total > (size_t)trail_count)
            first = total - (size_t)trail_count;
        const size_t display_count = total - first;

        std::vector<easy3d::vec3> face_colors;
        face_colors.reserve(display_count);

        easy3d::SurfaceMeshBuilder builder(aw3.gate_mesh);
        builder.begin_surface();
        for (size_t j = 0; j < display_count; ++j) {
            const auto& ev = aw3.gate_events[first + j];
            auto v0 = builder.add_vertex(easy3d::vec3(
                (float)ev.gate_p0[0], (float)ev.gate_p0[1], (float)ev.gate_p0[2]));
            auto v1 = builder.add_vertex(easy3d::vec3(
                (float)ev.gate_p1[0], (float)ev.gate_p1[1], (float)ev.gate_p1[2]));
            auto v2 = builder.add_vertex(easy3d::vec3(
                (float)ev.gate_p2[0], (float)ev.gate_p2[1], (float)ev.gate_p2[2]));
            builder.add_face({v0, v1, v2});

            const float t = display_count <= 1
                ? 1.0f
                : static_cast<float>(j) / static_cast<float>(display_count - 1);
            const easy3d::vec3 old_color(0.35f, 0.08f, 0.02f);
            const easy3d::vec3 new_color(1.0f, 0.25f, 0.05f);
            face_colors.push_back(old_color * (1.0f - t) + new_color * t);
        }
        builder.end_surface(false);

        auto face_color = aw3.gate_mesh->face_property<easy3d::vec3>(
            "f:color", easy3d::vec3(1.0f, 0.25f, 0.05f));
        size_t color_idx = 0;
        for (auto f : aw3.gate_mesh->faces()) {
            if (color_idx < face_colors.size())
                face_color[f] = face_colors[color_idx++];
        }

        if (created) {
            viewer_.add_model(aw3.gate_mesh);
            viewer_.register_model_tree_overlay(aw3.gate_mesh, saved_current);
        }

        auto* fd = aw3.gate_mesh->renderer()->get_triangles_drawable("faces", false);
        if (fd) {
            fd->set_property_coloring(easy3d::State::FACE, "f:color");
            fd->set_opacity(0.50f);
            fd->set_smooth_shading(false);
        }
        auto* ed = aw3.gate_mesh->renderer()->get_lines_drawable("edges", false);
        if (ed) {
            ed->set_visible(true);
            ed->set_uniform_coloring(easy3d::vec4(1.0f, 0.95f, 0.2f, 1.0f));
            ed->set_line_width(display_count <= 1 ? 3.0f : 1.5f);
            ed->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        }
        aw3.gate_mesh->renderer()->update();
    }

    if (saved_current && model_is_live(viewer_, saved_current))
        viewer_.set_current_model(saved_current);
    viewer_.mark_dirty();
}
