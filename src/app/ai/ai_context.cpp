// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_context.h"
#include "viewport/viewport_canvas.h"
#include <model_health.h>
#include "history/operation_history.h"
#include "platform/runtime_probe.h"
#include "services/platform/platform_runtime.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/state.h>

#include "imgui.h"

#include <sstream>
#include <thread>
#include <vector>
#include <algorithm>


AIContext& AIContext::instance() {
    static AIContext s;
    return s;
}


void AIContext::probe_runtime() {
    if (runtime_probed_) return;
    runtime_probed_ = true;

    cpu_threads_ = static_cast<int>(std::thread::hardware_concurrency());
    total_memory_mb_ = claw3d::platform::total_physical_memory_mb();
    os_name_ = claw3d::platform::operating_system_name();

    const claw3d::app::GraphicsRuntimeInfo graphics =
        claw3d::app::probe_graphics_runtime();
    gpu_renderer_ = graphics.renderer;
    gl_version_ = graphics.version;
}


void AIContext::set_active_panel(const std::string& panel_id) {
    std::lock_guard<std::mutex> lk(mu_);
    active_panel_ = panel_id;
}

std::string AIContext::active_panel() const {
    std::lock_guard<std::mutex> lk(mu_);
    return active_panel_;
}


std::string AIContext::build_scene_context(ViewportCanvas* viewer) const {
    if (!viewer) return {};
    const auto& models = viewer->models();
    std::ostringstream os;
    os << "Models in scene: " << models.size();
    int shown = 0;
    for (const auto& msp : models) {
        if (shown++ >= 8) { os << "\n  ... (truncated)"; break; }
        auto* m = msp.get();
        const char* t = "?";
        std::size_t n = 0;
        if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) {
            t = "Mesh"; n = sm->n_faces();
        } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) {
            t = "Cloud"; n = pc->n_vertices();
        } else if (auto* g = dynamic_cast<easy3d::Graph*>(m)) {
            t = "Graph"; n = g->n_edges();
        }
        os << "\n  - " << m->name() << " [" << t << ", " << n << "]";
    }
    return os.str();
}


namespace {

// One-line summary used inside the lineage chain.
std::string short_model_summary(easy3d::Model* m) {
    if (!m) return "(null)";
    std::ostringstream os;
    os << m->name();
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
        os << " [Mesh, V=" << sm->n_vertices() << ", F=" << sm->n_faces() << "]";
    else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m))
        os << " [Cloud, V=" << pc->n_vertices() << "]";
    else if (auto* g = dynamic_cast<easy3d::Graph*>(m))
        os << " [Graph, V=" << g->n_vertices() << ", E=" << g->n_edges() << "]";
    return os.str();
}

bool is_user_visible_scalar_vertex(easy3d::SurfaceMesh* m, const std::string& name) {
    if (name == "v:connectivity" || name == "v:point" || name == "v:normal"
        || name == "v:color" || name == "v:texcoord" || name == "v:deleted")
        return false;
    return m->get_vertex_property<float>(name) || m->get_vertex_property<double>(name)
        || m->get_vertex_property<int>(name) || m->get_vertex_property<unsigned int>(name);
}

bool is_user_visible_scalar_face(easy3d::SurfaceMesh* m, const std::string& name) {
    if (name == "f:connectivity" || name == "f:normal"
        || name == "f:color" || name == "f:deleted")
        return false;
    return m->get_face_property<float>(name) || m->get_face_property<double>(name)
        || m->get_face_property<int>(name) || m->get_face_property<unsigned int>(name);
}

bool is_user_visible_scalar_vertex(easy3d::PointCloud* m, const std::string& name) {
    if (name == "v:point" || name == "v:normal"
        || name == "v:color" || name == "v:texcoord" || name == "v:deleted")
        return false;
    return m->get_vertex_property<float>(name) || m->get_vertex_property<double>(name)
        || m->get_vertex_property<int>(name) || m->get_vertex_property<unsigned int>(name);
}

std::string join_limited(const std::vector<std::string>& values, std::size_t limit = 8) {
    if (values.empty()) return "none";
    std::ostringstream os;
    for (std::size_t i = 0; i < values.size() && i < limit; ++i) {
        if (i) os << ", ";
        os << values[i];
    }
    if (values.size() > limit)
        os << ", ...";
    return os.str();
}

const char* coloring_method_name(easy3d::State::Method m) {
    switch (m) {
        case easy3d::State::UNIFORM_COLOR: return "uniform";
        case easy3d::State::COLOR_PROPERTY: return "color_property";
        case easy3d::State::TEXTURED: return "texture";
        case easy3d::State::SCALAR_FIELD: return "scalar_field";
    }
    return "?";
}

const char* property_location_name(easy3d::State::Location loc) {
    switch (loc) {
        case easy3d::State::VERTEX: return "vertex";
        case easy3d::State::FACE: return "face";
        case easy3d::State::EDGE: return "edge";
        case easy3d::State::HALFEDGE: return "halfedge";
    }
    return "?";
}

// Extract the operation tag from a derived model name. We named derived
// models <parent>.<op>[-param] (e.g. bear.sampled, bear.simpl-lt,
// bear.sampled.poisson). The op is whatever follows the last '.' in the
// child's name that the parent's name doesn't already cover. Returns ""
// when child is not actually derived from parent by name.
std::string derived_op_from_names(const std::string& parent,
                                  const std::string& child) {
    if (child.size() <= parent.size() + 1) return {};
    if (child.compare(0, parent.size(), parent) != 0) return {};
    if (child[parent.size()] != '.') return {};
    return child.substr(parent.size() + 1);
}

} // namespace


std::string AIContext::build_model_context(ViewportCanvas* viewer,
                                           easy3d::Model* m) const {
    if (!m) return "Current model: (none selected)";
    std::ostringstream os;
    os << "Name: " << m->name() << "\n";

    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) {
        os << "Type: SurfaceMesh\n"
           << "Vertices: " << sm->n_vertices()
           << ", Edges: " << sm->n_edges()
           << ", Faces: " << sm->n_faces() << "\n";
        bool has_uv = (bool)sm->get_vertex_property<easy3d::vec2>("v:texcoord")
                   || (bool)sm->get_halfedge_property<easy3d::vec2>("h:texcoord");
        bool has_vc = (bool)sm->get_vertex_property<easy3d::vec3>("v:color");
        bool has_fc = (bool)sm->get_face_property<easy3d::vec3>("f:color");
        bool has_vn = (bool)sm->get_vertex_property<easy3d::vec3>("v:normal");
        os << "Attributes: uv=" << (has_uv ? "yes" : "no")
           << ", vcolor=" << (has_vc ? "yes" : "no")
           << ", fcolor=" << (has_fc ? "yes" : "no")
           << ", vnormal=" << (has_vn ? "yes" : "no") << "\n";
        std::vector<std::string> vertex_scalars;
        std::vector<std::string> face_scalars;
        for (const auto& name : sm->vertex_properties())
            if (is_user_visible_scalar_vertex(sm, name))
                vertex_scalars.push_back(name);
        for (const auto& name : sm->face_properties())
            if (is_user_visible_scalar_face(sm, name))
                face_scalars.push_back(name);
        os << "Vertex scalar fields: " << join_limited(vertex_scalars) << "\n"
           << "Face scalar fields: " << join_limited(face_scalars) << "\n";
        if (auto* faces = sm->renderer()->get_triangles_drawable("faces")) {
            os << "Active face display: method=" << coloring_method_name(faces->coloring_method())
               << ", location=" << property_location_name(faces->property_location())
               << ", property=" << (faces->property_name().empty() ? "(none)" : faces->property_name())
               << "\n";
        }
    } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) {
        os << "Type: PointCloud\n"
           << "Points: " << pc->n_vertices() << "\n";
        bool has_n = (bool)pc->get_vertex_property<easy3d::vec3>("v:normal");
        bool has_c = (bool)pc->get_vertex_property<easy3d::vec3>("v:color");
        os << "Attributes: normal=" << (has_n ? "yes" : "no")
           << ", color=" << (has_c ? "yes" : "no") << "\n";
        std::vector<std::string> scalars;
        for (const auto& name : pc->vertex_properties())
            if (is_user_visible_scalar_vertex(pc, name))
                scalars.push_back(name);
        os << "Point scalar fields: " << join_limited(scalars) << "\n";
        if (auto* points = pc->renderer()->get_points_drawable("vertices")) {
            os << "Active point display: method=" << coloring_method_name(points->coloring_method())
               << ", location=" << property_location_name(points->property_location())
               << ", property=" << (points->property_name().empty() ? "(none)" : points->property_name())
               << "\n";
        }
    } else if (auto* g = dynamic_cast<easy3d::Graph*>(m)) {
        os << "Type: Graph\n"
           << "Vertices: " << g->n_vertices()
           << ", Edges: " << g->n_edges() << "\n";
    } else {
        os << "Type: Unknown\n";
    }

    const auto& bbox = m->bounding_box();
    if (bbox.is_valid()) {
        auto sz = bbox.max_point() - bbox.min_point();
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "BBox: %.3f x %.3f x %.3f (diagonal %.3f)",
            sz[0], sz[1], sz[2], easy3d::length(sz));
        os << buf;
    } else {
        os << "BBox: (invalid)";
    }

    // -- Health (if precomputed by the Health Report panel) --
    {
        std::string health = ModelHealthRegistry::instance().format_for_ai(m);
        if (!health.empty())
            os << "\n\nHealth:\n" << health;
    }

    // -- Lineage: root -> ... -> current --
    if (!viewer) return os.str();

    std::vector<easy3d::Model*> chain;
    chain.push_back(m);
    for (int safety = 0; safety < 32; ++safety) {
        auto* info = viewer->model_tree_info(chain.back());
        if (!info || !info->parent) break;
        chain.push_back(info->parent);
    }
    if (chain.size() <= 1) return os.str();  // no lineage

    std::reverse(chain.begin(), chain.end());  // root -> current
    os << "\n\nLineage (root -> current):\n";
    for (std::size_t i = 0; i < chain.size(); ++i) {
        os << "  " << (i + 1) << ". " << short_model_summary(chain[i]);
        if (i > 0) {
            std::string op = derived_op_from_names(chain[i-1]->name(),
                                                   chain[i]->name());
            if (!op.empty())
                os << "  (op: " << op << ")";
        }
        if (chain[i] == m) os << "  <-- current";
        os << "\n";
    }
    return os.str();
}


std::string AIContext::build_runtime_context() const {
    std::ostringstream os;
    os << "OS: " << (os_name_.empty() ? "?" : os_name_) << "\n"
       << "CPU threads: " << cpu_threads_;
    if (total_memory_mb_) {
        os << "\nTotal RAM: " << (total_memory_mb_ / 1024) << " GB";
    }
    if (!gpu_renderer_.empty())
        os << "\nGPU: " << gpu_renderer_;
    if (!gl_version_.empty())
        os << "\nOpenGL: " << gl_version_;
    return os.str();
}


std::string AIContext::build_active_panel_context() const {
    auto p = active_panel();
    if (p.empty()) return "Active panel: (none)";
    return "Active panel: " + p;
}


std::string AIContext::build_full_context(ViewportCanvas* viewer, unsigned flags) const {
    std::ostringstream os;
    bool first = true;
    auto sep = [&] { if (!first) os << "\n\n"; first = false; };

    if (flags & AICtx_CurrentModel) {
        easy3d::Model* m = viewer ? viewer->current_model() : nullptr;
        sep(); os << "[Current Model]\n" << build_model_context(viewer, m);
    }
    if (flags & AICtx_Scene) {
        std::string s = build_scene_context(viewer);
        if (!s.empty()) { sep(); os << "[Scene]\n" << s; }
    }
    if (flags & AICtx_ActivePanel) {
        sep(); os << "[Panel]\n" << build_active_panel_context();
    }
    if (flags & AICtx_Runtime) {
        sep(); os << "[Runtime]\n" << build_runtime_context();
    }
    // History always tacks on when there's anything to report. It's cheap
    // (5 entries cap) and helps the AI answer "what did I just do?"
    // questions even when the caller didn't ask for runtime context.
    {
        std::string h = OperationHistory::instance().format_for_ai(5);
        if (!h.empty()) { sep(); os << "[History]\n" << h; }
    }
    return os.str();
}


void claw_report_active_panel(const char* title) {
    if (!title) return;
    if (ImGui::IsWindowFocused())
        AIContext::instance().set_active_panel(title);
}
