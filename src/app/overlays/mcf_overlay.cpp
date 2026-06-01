// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "overlays/overlay_utils.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace {

easy3d::vec3 mcf_meso_color(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const easy3d::vec3 cyan   (0.18f, 0.78f, 0.85f);
    const easy3d::vec3 magenta(0.95f, 0.25f, 0.78f);
    return easy3d::vec3(cyan.x + (magenta.x - cyan.x) * (float)t,
                        cyan.y + (magenta.y - cyan.y) * (float)t,
                        cyan.z + (magenta.z - cyan.z) * (float)t);
}

bool mcf_preview_triangle_is_valid(
    const std::vector<easy3d::vec3>& points,
    int i0, int i1, int i2)
{
    const int nv = static_cast<int>(points.size());
    if (i0 < 0 || i1 < 0 || i2 < 0 ||
        i0 >= nv || i1 >= nv || i2 >= nv ||
        i0 == i1 || i1 == i2 || i2 == i0)
        return false;

    const easy3d::vec3& p0 = points[i0];
    const easy3d::vec3& p1 = points[i1];
    const easy3d::vec3& p2 = points[i2];
    if (!std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(p0.z) ||
        !std::isfinite(p1.x) || !std::isfinite(p1.y) || !std::isfinite(p1.z) ||
        !std::isfinite(p2.x) || !std::isfinite(p2.y) || !std::isfinite(p2.z))
        return false;

    const easy3d::vec3 e01 = p1 - p0;
    const easy3d::vec3 e02 = p2 - p0;
    const easy3d::vec3 e12 = p2 - p1;
    const float max_edge2 = std::max(e01.length2(),
                             std::max(e02.length2(), e12.length2()));
    if (max_edge2 <= FLT_MIN)
        return false;

    const float area2 = easy3d::cross(e01, e02).length2();
    const float eps = std::max(1e-20f, max_edge2 * max_edge2 * 1e-12f);
    return area2 > eps;
}

easy3d::vec3 mcf_sdf_color(double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const easy3d::vec3 blue  (0.18f, 0.42f, 0.92f);
    const easy3d::vec3 yellow(1.00f, 0.95f, 0.30f);
    const easy3d::vec3 red   (0.95f, 0.20f, 0.20f);
    if (t < 0.5) {
        const float k = (float)(t / 0.5);
        return easy3d::vec3(blue.x + (yellow.x - blue.x) * k,
                            blue.y + (yellow.y - blue.y) * k,
                            blue.z + (yellow.z - blue.z) * k);
    }
    const float k = (float)((t - 0.5) / 0.5);
    return easy3d::vec3(yellow.x + (red.x - yellow.x) * k,
                        yellow.y + (red.y - yellow.y) * k,
                        yellow.z + (red.z - yellow.z) * k);
}

} // namespace

void MainWindow::init_mcf_overlay(easy3d::SurfaceMesh* src) {
    clear_mcf_overlay();
    if (!src) return;
    auto& state = algorithm_overlay_.mcf;
    state.source_ghost = src;
    state.source_saved_visible = src->renderer()->is_visible();
    if (auto* fd = src->renderer()->get_triangles_drawable("faces", false)) {
        state.source_saved_face_visible = fd->is_visible();
        state.source_saved_opacity = fd->opacity();
        fd->set_visible(false);
    }
    if (auto* ed = src->renderer()->get_lines_drawable("edges", false)) {
        state.source_saved_edge_visible = ed->is_visible();
        state.source_saved_edge_coloring_method =
            static_cast<int>(ed->coloring_method());
        state.source_saved_edge_color = ed->color();
        state.source_saved_edge_width = ed->line_width();
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.55f, 0.55f, 0.55f, 0.55f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    viewer_.mark_dirty();
}

void MainWindow::update_mcf_overlay(const MCF_Snapshot& snap) {
    if (snap.vertices.empty() || snap.triangles.empty()) return;
    auto& state = algorithm_overlay_.mcf;
    delete_model_if_live(viewer_, state.meso_overlay);
    state.meso_overlay = create_surface_overlay(viewer_, "mcf_meso");
    std::vector<easy3d::SurfaceMesh::Vertex> vh;
    std::vector<easy3d::vec3> preview_points;
    vh.reserve(snap.vertices.size());
    preview_points.reserve(snap.vertices.size());
    for (const auto& p : snap.vertices) {
        const easy3d::vec3 point((float)p.x, (float)p.y, (float)p.z);
        preview_points.push_back(point);
        vh.push_back(state.meso_overlay->add_vertex(point));
    }
    for (const auto& t : snap.triangles) {
        if (!mcf_preview_triangle_is_valid(preview_points, t.v0, t.v1, t.v2))
            continue;
        state.meso_overlay->add_triangle(vh[t.v0], vh[t.v1], vh[t.v2]);
    }
    const easy3d::vec3 face_col = mcf_meso_color(snap.metrics.progress);
    if (auto* fd = state.meso_overlay->renderer()->get_triangles_drawable(
            "faces", false)) {
        fd->set_uniform_coloring(easy3d::vec4(face_col, 1.0f));
        fd->set_lighting(false);
        fd->set_distinct_back_color(false);
        fd->set_opacity(1.0f);
        fd->update();
    }
    if (auto* ed = state.meso_overlay->renderer()->get_lines_drawable(
            "edges", false)) {
        ed->set_visible(true);
        ed->set_uniform_coloring(easy3d::vec4(0.78f, 0.20f, 0.98f, 1.0f));
        ed->set_line_width(1.0f);
        ed->update();
    }
    state.meso_overlay->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::paint_mcf_sdf_on_source(easy3d::SurfaceMesh* src,
                                              const std::vector<double>& sdf,
                                              bool on) {
    if (!src) return;
    if (!model_is_live(viewer_, src)) return;

    if (on) {
        auto col = src->get_vertex_property<easy3d::vec3>("v:color");
        if (!col)
            col = src->add_vertex_property<easy3d::vec3>(
                "v:color", easy3d::vec3(0.5f, 0.5f, 0.5f));
        double dmax = 0.0;
        for (double d : sdf) if (d > dmax) dmax = d;
        const double denom = (dmax > 1e-12) ? dmax : 1.0;
        const int nsdf = (int)sdf.size();
        for (auto v : src->vertices()) {
            const int i = (int)v.idx();
            if (i < nsdf && sdf[i] >= 0.0)
                col[v] = mcf_sdf_color(sdf[i] / denom);
            else
                col[v] = easy3d::vec3(0.4f, 0.4f, 0.4f);
        }
        if (auto* fd = src->renderer()->get_triangles_drawable("faces", false)) {
            fd->set_visible(true);
            fd->set_opacity(1.0f);
            fd->set_lighting(false);
            fd->set_distinct_back_color(false);
            fd->set_property_coloring(easy3d::State::VERTEX, "v:color");
            fd->update();
        }
        if (auto* ed = src->renderer()->get_lines_drawable("edges", false)) {
            ed->set_visible(false);
            ed->update();
        }
    } else {
        if (auto* fd = src->renderer()->get_triangles_drawable("faces", false)) {
            fd->set_visible(false);
            fd->set_coloring(easy3d::State::UNIFORM_COLOR,
                             easy3d::State::VERTEX, "v:point");
            fd->set_uniform_coloring(easy3d::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            fd->update();
        }
        if (auto* ed = src->renderer()->get_lines_drawable("edges", false)) {
            ed->set_visible(true);
            ed->set_uniform_coloring(easy3d::vec4(0.55f, 0.55f, 0.55f, 0.55f));
            ed->set_line_width(1.0f);
            ed->update();
        }
    }
    src->renderer()->update();
    viewer_.mark_dirty();
}

void MainWindow::update_mcf_correspondence_overlay(
    const std::vector<MCF_Line>& lines) {
    auto& state = algorithm_overlay_.mcf;
    delete_model_if_live(viewer_, state.correspondence_graph);
    if (lines.empty()) {
        viewer_.mark_dirty();
        return;
    }
    auto* g = create_graph_overlay(viewer_, "mcf_correspondence");
    for (const auto& ln : lines) {
        auto va = g->add_vertex(easy3d::vec3(
            (float)ln.a.x, (float)ln.a.y, (float)ln.a.z));
        auto vb = g->add_vertex(easy3d::vec3(
            (float)ln.b.x, (float)ln.b.y, (float)ln.b.z));
        g->add_edge(va, vb);
    }
    if (auto* ld = g->renderer()->get_lines_drawable("edges", false)) {
        ld->set_uniform_coloring(easy3d::vec4(0.55f, 0.55f, 0.65f, 0.75f));
        ld->set_line_width(1.0f);
        ld->set_visible(true);
        ld->update();
    }
    if (auto* vd = g->renderer()->get_points_drawable("vertices", false)) {
        vd->set_visible(false);
        vd->update();
    }
    g->renderer()->update();
    state.correspondence_graph = g;
    viewer_.mark_dirty();
}

void MainWindow::clear_mcf_correspondence_overlay() {
    delete_model_if_live(viewer_, algorithm_overlay_.mcf.correspondence_graph);
    viewer_.mark_dirty();
}

void MainWindow::clear_mcf_overlay(bool restore_source) {
    auto& state = algorithm_overlay_.mcf;
    delete_model_if_live(viewer_, state.meso_overlay);
    delete_model_if_live(viewer_, state.correspondence_graph);
    if (restore_source && state.source_ghost &&
        model_is_live(viewer_, state.source_ghost)) {
        state.source_ghost->renderer()->set_visible(state.source_saved_visible);
        if (auto* fd = state.source_ghost->renderer()->get_triangles_drawable(
                "faces", false)) {
            fd->set_visible(state.source_saved_face_visible);
            fd->set_opacity(state.source_saved_opacity);
            fd->set_coloring(easy3d::State::UNIFORM_COLOR,
                             easy3d::State::VERTEX, "v:point");
            fd->update();
        }
        if (auto* ed = state.source_ghost->renderer()->get_lines_drawable(
                "edges", false)) {
            ed->set_visible(state.source_saved_edge_visible);
            ed->set_coloring(
                static_cast<easy3d::State::Method>(
                    state.source_saved_edge_coloring_method),
                easy3d::State::VERTEX, "v:point");
            ed->set_uniform_coloring(state.source_saved_edge_color);
            ed->set_line_width(state.source_saved_edge_width);
            ed->update();
        }
        state.source_ghost->renderer()->update();
        state.source_ghost = nullptr;
    }
    viewer_.mark_dirty();
}

bool MainWindow::set_mcf_source_ghost_visible(bool visible) {
    auto& state = algorithm_overlay_.mcf;
    if (!state.source_ghost || !model_is_live(viewer_, state.source_ghost)) {
        state.source_ghost = nullptr;
        return false;
    }
    state.source_ghost->renderer()->set_visible(visible);
    viewer_.mark_dirty();
    return true;
}

bool MainWindow::set_mcf_meso_overlay_visible(bool visible) {
    auto& state = algorithm_overlay_.mcf;
    if (!state.meso_overlay || !model_is_live(viewer_, state.meso_overlay)) {
        state.meso_overlay = nullptr;
        return false;
    }
    state.meso_overlay->renderer()->set_visible(visible);
    viewer_.mark_dirty();
    return true;
}
