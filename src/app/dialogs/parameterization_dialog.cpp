// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/parameterization_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_language.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/parameterization_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

static const char* PARAM_HELP_PROMPT =
    "I am using CGAL LSCM (Least Squares Conformal Maps) UV parameterization "
    "in 3D Claw.\n\n"
    "LSCM is a free-boundary, angle-preserving (conformal) parameterization.\n"
    "It maps a 3D triangle mesh onto a 2D plane.\n\n"
    "Requirements:\n"
    "  - Triangle mesh (non-triangle meshes are rejected).\n"
    "  - Open boundary OR user-created seams for closed meshes.\n"
    "  - Mesh must be a topological disc after cutting.\n\n"
    "Key metrics:\n"
    "  - Area distortion: log(3D_area / UV_area); 0 = perfect.\n"
    "  - Angle distortion: average angle difference per corner (radians).\n"
    "  - Flipped triangles: UV orientation reversed = invalid.\n\n"
    "Please suggest parameter settings and interpretation of results.";

namespace {

// --- Distortion color: blue (good) -> green -> yellow -> red (bad) ---
// Shared between the UV-view ImU32 fill colors and the per-vertex vec3 colors
// stamped onto the 3D mesh, so both views use the same gradient.

void distortion_color_rgb(double t, float& r, float& g, float& b) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    if (t < 0.5) {
        float k = (float)(t * 2.0);
        r = k; g = 0.2f + 0.8f * k; b = 1.0f - k;
    } else {
        float k = (float)((t - 0.5) * 2.0);
        r = 1.0f; g = 1.0f - 0.8f * k; b = 0.0f;
    }
}

ImU32 distortion_color(double t) {
    float r, g, b;
    distortion_color_rgb(t, r, g, b);
    return IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 255);
}

// Map area_distortion [0, 2.0] to [0, 1].
double distortion_t(double area_distortion) {
    return std::min(1.0, area_distortion / 2.0);
}

// --- UV triangle picking: point-in-triangle test ---

bool point_in_uv_triangle(double px, double py,
                          double u0, double v0,
                          double u1, double v1,
                          double u2, double v2)
{
    const double d1 = (px - u1) * (v0 - v1) - (u0 - u1) * (py - v1);
    const double d2 = (px - u2) * (v1 - v2) - (u1 - u2) * (py - v2);
    const double d3 = (px - u0) * (v2 - v0) - (u2 - u0) * (py - v0);
    const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

// --- Barycentric coordinates ---

// 2D barycentric of (px, py) in triangle (u0,v0)(u1,v1)(u2,v2).
// Returns (w0, w1, w2) summing to 1.
void barycentric_2d(double px, double py,
                    double u0, double v0,
                    double u1, double v1,
                    double u2, double v2,
                    float& w0, float& w1, float& w2)
{
    const double den = (v1 - v2) * (u0 - u2) + (u2 - u1) * (v0 - v2);
    if (std::abs(den) < 1e-20) { w0 = 1.0f; w1 = w2 = 0.0f; return; }
    const double inv = 1.0 / den;
    w0 = (float)(((v1 - v2) * (px - u2) + (u2 - u1) * (py - v2)) * inv);
    w1 = (float)(((v2 - v0) * (px - u2) + (u0 - u2) * (py - v2)) * inv);
    w2 = 1.0f - w0 - w1;
}

// 3D barycentric of point p in triangle (a, b, c) via projection onto the
// face plane. Returns (w0, w1, w2). The picker has already constrained p to
// lie close to the face, so a plane-projected solve is enough.
void barycentric_3d(const easy3d::vec3& p,
                    const easy3d::vec3& a,
                    const easy3d::vec3& b,
                    const easy3d::vec3& c,
                    float& w0, float& w1, float& w2)
{
    const easy3d::vec3 v0 = b - a;
    const easy3d::vec3 v1 = c - a;
    const easy3d::vec3 v2 = p - a;
    const double d00 = dot(v0, v0);
    const double d01 = dot(v0, v1);
    const double d11 = dot(v1, v1);
    const double d20 = dot(v2, v0);
    const double d21 = dot(v2, v1);
    const double den = d00 * d11 - d01 * d01;
    if (std::abs(den) < 1e-20) { w0 = 1.0f; w1 = w2 = 0.0f; return; }
    const double inv = 1.0 / den;
    w1 = (float)((d11 * d20 - d01 * d21) * inv);
    w2 = (float)((d00 * d21 - d01 * d20) * inv);
    w0 = 1.0f - w1 - w2;
}

// --- Picking helpers ---

// Apply face highlight on the 3D mesh "faces" drawable.
void highlight_3d_face(easy3d::SurfaceMesh* mesh, int fid) {
    if (!mesh) return;
    auto* fd = mesh->renderer()->get_triangles_drawable("faces", false);
    if (!fd) return;
    if (fid < 0) {
        fd->set_highlight(false);
        fd->set_highlight_range({-1, -1});
        return;
    }
    auto trange = mesh->get_face_property<std::pair<int,int>>("f:triangle_range");
    if (!trange) return;
    auto f = easy3d::SurfaceMesh::Face(fid);
    if (!f.is_valid()) return;
    fd->set_highlight(true);
    fd->set_highlight_range(trange[f]);
}

// 3D pick -> UV: face/point picked on the 3D mesh, compute barycentric
// against the 3D vertex positions, then re-weight the per-corner UVs using
// the matching vertex IDs (face_uvs order may differ from mesh traversal).
void apply_3d_pick(ParameterizationState& s, easy3d::SurfaceMesh* mesh,
                   easy3d::SurfaceMesh::Face face, const easy3d::vec3& pt)
{
    int fid = (int)face.idx();
    easy3d::vec3 v_pos[3];
    int v_idx[3] = {-1, -1, -1};
    int k = 0;
    for (auto v : mesh->vertices(face)) {
        if (k < 3) {
            v_pos[k] = mesh->position(v);
            v_idx[k] = (int)v.idx();
        }
        ++k;
    }
    if (k != 3) return;

    float w[3];
    barycentric_3d(pt, v_pos[0], v_pos[1], v_pos[2], w[0], w[1], w[2]);

    const auto& fuvs = s.last_result.face_uvs;
    if (fid < 0 || fid >= (int)fuvs.size()) return;
    const auto& fu = fuvs[fid];

    float w_uv[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (v_idx[i] == fu.vertex_id[j]) { w_uv[j] += w[i]; break; }
        }
    }

    const double u = w_uv[0]*fu.uv[0].u + w_uv[1]*fu.uv[1].u + w_uv[2]*fu.uv[2].u;
    const double v = w_uv[0]*fu.uv[0].v + w_uv[1]*fu.uv[1].v + w_uv[2]*fu.uv[2].v;

    s.picked_3d_face = fid;
    s.picked_3d_x = pt.x; s.picked_3d_y = pt.y; s.picked_3d_z = pt.z;
    s.uv_picked_face = fid;
    s.uv_picked_u = (float)u;
    s.uv_picked_v = (float)v;
    highlight_3d_face(mesh, fid);
    auto* win = MainWindow::instance();
    if (win && win->viewer()) win->viewer()->mark_dirty();
    std::snprintf(s.pick_status, sizeof(s.pick_status),
        "3D->UV: face=%d UV=(%.3f, %.3f)", fid, u, v);
}

// UV pick -> 3D: face/UV coords picked in the 2D panel, compute UV
// barycentric against the per-corner UVs, then interpolate the 3D position
// using the matching mesh vertices.
void apply_uv_pick(ParameterizationState& s, easy3d::SurfaceMesh* mesh) {
    int fid = s.uv_picked_face;
    if (fid < 0) return;
    const auto& fuvs = s.last_result.face_uvs;
    if (fid >= (int)fuvs.size()) return;
    const auto& fu = fuvs[fid];

    float w[3];
    barycentric_2d((double)s.uv_picked_u, (double)s.uv_picked_v,
                    fu.uv[0].u, fu.uv[0].v,
                    fu.uv[1].u, fu.uv[1].v,
                    fu.uv[2].u, fu.uv[2].v,
                    w[0], w[1], w[2]);

    easy3d::vec3 p(0, 0, 0);
    for (int j = 0; j < 3; ++j) {
        int vid = fu.vertex_id[j];
        if (vid < 0 || vid >= (int)mesh->n_vertices()) continue;
        auto vh = easy3d::SurfaceMesh::Vertex(vid);
        if (!vh.is_valid()) continue;
        p += w[j] * mesh->position(vh);
    }
    s.picked_3d_face = fid;
    s.picked_3d_x = p.x; s.picked_3d_y = p.y; s.picked_3d_z = p.z;
    highlight_3d_face(mesh, fid);
    auto* win = MainWindow::instance();
    if (win && win->viewer()) win->viewer()->mark_dirty();
    std::snprintf(s.pick_status, sizeof(s.pick_status),
        "UV->3D: face=%d 3D=(%.3f, %.3f, %.3f)", fid, p.x, p.y, p.z);
}

bool build_arap_snapshot_face_uvs(const PARAM_Result& r, int frame,
                                  std::vector<PARAM_FaceUV>& out) {
    if (frame < 0 || frame >= (int)r.arap_snapshots.size() ||
        r.face_uvs.empty())
        return false;
    const auto& snap = r.arap_snapshots[frame];
    out = r.face_uvs;
    for (auto& fu : out) {
        for (int i = 0; i < 3; ++i) {
            const int vid = fu.vertex_id[i];
            if (vid >= 0 && vid < (int)snap.vertex_uvs.size())
                fu.uv[i] = snap.vertex_uvs[vid];
        }
    }
    return true;
}

void clear_pick(ParameterizationState& s, easy3d::SurfaceMesh* mesh) {
    s.pick_mode = 0;
    s.picked_3d_face = -1;
    s.uv_picked_face = -1;
    s.pick_status[0] = 0;
    highlight_3d_face(mesh, -1);
    auto* win = MainWindow::instance();
    if (win && win->viewer()) win->viewer()->mark_dirty();
}

// --- Seam helpers ---

// Find the closest vertex among the 3 face corners to a 3D point.
int nearest_face_vertex(easy3d::SurfaceMesh* mesh,
                         easy3d::SurfaceMesh::Face face,
                         const easy3d::vec3& pt) {
    int best = -1;
    float best_d2 = std::numeric_limits<float>::max();
    for (auto v : mesh->vertices(face)) {
        const auto& p = mesh->position(v);
        float dx = p.x - pt.x, dy = p.y - pt.y, dz = p.z - pt.z;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < best_d2) { best_d2 = d2; best = (int)v.idx(); }
    }
    return best;
}

// Dijkstra shortest path over mesh edges between two vertices.
// Returns true on success and fills out_vids with the vertex sequence
// (including endpoints) and out_length with the total euclidean length.
bool shortest_path_on_mesh(easy3d::SurfaceMesh* mesh,
                            int start_vid, int end_vid,
                            std::vector<int>& out_vids,
                            double& out_length)
{
    out_vids.clear();
    out_length = 0.0;
    if (!mesh || start_vid < 0 || end_vid < 0) return false;
    const int nv = (int)mesh->n_vertices();
    if (start_vid >= nv || end_vid >= nv) return false;
    if (start_vid == end_vid) {
        out_vids.push_back(start_vid);
        return true;
    }
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    std::vector<double> dist(nv, std::numeric_limits<double>::infinity());
    std::vector<int>    prev(nv, -1);
    using QItem = std::pair<double,int>;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
    dist[start_vid] = 0.0;
    pq.push(QItem(0.0, start_vid));
    while (!pq.empty()) {
        QItem top = pq.top(); pq.pop();
        double d = top.first;
        int u = top.second;
        if (u == end_vid) break;
        if (d > dist[u]) continue;
        easy3d::SurfaceMesh::Vertex vu(u);
        const easy3d::vec3& pu = pts[vu];
        for (easy3d::SurfaceMesh::Vertex vh : mesh->vertices(vu)) {
            int w = (int)vh.idx();
            const easy3d::vec3& pw = pts[vh];
            double dx = pu.x - pw.x, dy = pu.y - pw.y, dz = pu.z - pw.z;
            double edge_len = std::sqrt(dx*dx + dy*dy + dz*dz);
            double nd = d + edge_len;
            if (nd < dist[w]) {
                dist[w] = nd;
                prev[w] = u;
                pq.push(QItem(nd, w));
            }
        }
    }
    if (!std::isfinite(dist[end_vid])) return false;
    // Reconstruct
    std::vector<int> rev;
    for (int cur = end_vid; cur != -1; cur = prev[cur]) rev.push_back(cur);
    std::reverse(rev.begin(), rev.end());
    if (rev.empty() || rev.front() != start_vid) return false;
    out_vids = std::move(rev);
    out_length = dist[end_vid];
    return true;
}

// Rebuild the seam visualisation drawables on the source mesh's Renderer.
// Two drawables are owned by the mesh: a yellow "param_seams" LinesDrawable
// for committed paths, and a "param_seam_endpoints" PointsDrawable for the
// pending start/end markers. Both are toggleable via show_seams.
void rebuild_seam_overlay(easy3d::SurfaceMesh* mesh,
                           const ParameterizationState& s)
{
    if (!mesh) return;
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    auto* renderer = mesh->renderer();

    auto* lines = renderer->get_lines_drawable("param_seams", false);
    if (!lines) lines = renderer->add_lines_drawable("param_seams");
    auto* points = renderer->get_points_drawable("param_seam_endpoints", false);
    if (!points) points = renderer->add_points_drawable("param_seam_endpoints");

    // Lines: every consecutive (v_i, v_{i+1}) along each committed path.
    std::vector<easy3d::vec3> lv;
    std::vector<unsigned int> li;
    for (const auto& sp : s.seam_paths) {
        for (size_t i = 0; i + 1 < sp.vertex_ids.size(); ++i) {
            int a = sp.vertex_ids[i], b = sp.vertex_ids[i+1];
            if (a < 0 || b < 0) continue;
            unsigned int base = (unsigned int)lv.size();
            lv.push_back(pts[easy3d::SurfaceMesh::Vertex(a)]);
            lv.push_back(pts[easy3d::SurfaceMesh::Vertex(b)]);
            li.push_back(base);
            li.push_back(base + 1);
        }
    }
    if (!lv.empty()) {
        lines->update_vertex_buffer(lv);
        lines->update_element_buffer(li);
    } else {
        lines->update_vertex_buffer({});
        lines->update_element_buffer(std::vector<unsigned int>{});
    }
    lines->set_uniform_coloring(easy3d::vec4(1.0f, 0.78f, 0.18f, 1.0f)); // amber
    lines->set_line_width(3.5f);
    lines->set_visible(s.show_seams);
    lines->set_impostor_type(easy3d::LinesDrawable::CYLINDER);

    // Two uniform-colored points drawables for pending start/end markers --
    // separate drawables keep the colors distinct without per-vertex color
    // plumbing through the mesh's vertex_property system.
    (void)points; // keep the "param_seam_endpoints" drawable around as a stub
    points->set_visible(false);

    auto setup_endpoint = [&](const char* name, int vid,
                              const easy3d::vec4& color) {
        auto* d = renderer->get_points_drawable(name, false);
        if (!d) d = renderer->add_points_drawable(name);
        if (vid < 0 || vid >= (int)mesh->n_vertices()) {
            d->set_visible(false);
            return;
        }
        std::vector<easy3d::vec3> v{pts[easy3d::SurfaceMesh::Vertex(vid)]};
        d->update_vertex_buffer(v);
        d->set_uniform_coloring(color);
        d->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        d->set_point_size(16.0f);
        d->set_visible(s.show_seams);
    };
    setup_endpoint("param_seam_start", s.seam_pending_start,
                   easy3d::vec4(0.15f, 1.0f, 0.35f, 1.0f));
    setup_endpoint("param_seam_end", s.seam_pending_end,
                   easy3d::vec4(1.0f, 0.25f, 0.25f, 1.0f));

    auto* win = MainWindow::instance();
    if (win && win->viewer()) win->viewer()->mark_dirty();
}

void clear_seams(ParameterizationState& s, easy3d::SurfaceMesh* mesh) {
    s.seam_paths.clear();
    s.seam_pending_start = -1;
    s.seam_pending_end   = -1;
    s.seam_pick_mode     = 0;
    s.seam_status[0]     = 0;
    rebuild_seam_overlay(mesh, s);
}

// --- AI helpers ---

// Route the ASCII filter, send-prompt helper, and mesh metadata collection
// through the shared claw_ai layer. UV-specific result metrics still stay
// local to this dialog.
using claw_ai::ascii_only;

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

void append_param_mesh_metadata(std::ostringstream& oss,
                                easy3d::SurfaceMesh* mesh) {
    const auto m = claw_ai::collect_surface_mesh_ai_stats(mesh);
    oss << "Mesh: " << m.name << "\n"
        << "Vertices: " << m.vertices << " Faces: " << m.faces << "\n"
        << "Triangle: " << (m.triangle_mesh ? "yes" : "no")
        << "  Closed: " << (m.closed ? "yes" : "no") << "\n";
    if (m.bbox_valid)
        oss << "BBox diagonal: " << m.bbox_diag << "\n";
}

std::string build_param_advice_prompt(easy3d::SurfaceMesh* mesh) {
    std::ostringstream oss;
    oss << "Please give parameter advice for CGAL LSCM UV parameterization.\n\n"
        << "Algorithm: LSCM (Least Squares Conformal Maps), angle-preserving,\n"
        << "free-boundary. Two vertices are pinned for a unique solution.\n\n";
    if (!mesh) return ascii_only(oss.str());
    append_param_mesh_metadata(oss, mesh);
    oss << "\n" << ai_lang::directive() << "\n"
        << "1. Is this mesh suitable for LSCM?\n"
        << "2. Any pre-processing needed?\n"
        << "3. Expected distortion quality?\n"
        << "4. If closed: seam strategy?\n";
    return ascii_only(oss.str());
}

std::string build_param_evaluation_prompt(const ParameterizationState& s,
                                            easy3d::SurfaceMesh* mesh) {
    std::ostringstream oss;
    oss << "Please evaluate this CGAL LSCM UV parameterization result.\n\n";
    const auto& r = s.last_result;
    if (r.error_code != PARAM_ERR_None) {
        oss << "Status: FAILED (code=" << param_error_code_value(r.error_code) << ")\n"
            << "Error: " << r.error_message << "\n";
        return ascii_only(oss.str());
    }
    oss << "Status: SUCCESS\n"
        << "Output faces: " << r.face_uvs.size() << "\n"
        << "Boundary edges: " << r.boundary_edge_count
        << "  Closed: " << (r.is_closed ? "yes" : "no") << "\n"
        << "Elapsed: " << r.elapsed_ms << " ms\n\n"
        << "Area distortion: mean=" << r.mean_area_distortion
        << " max=" << r.max_area_distortion << "\n"
        << "Angle distortion: mean=" << r.mean_angle_distortion
        << " max=" << r.max_angle_distortion << "\n"
        << "Flipped faces: " << r.flipped_count
        << "  Degenerate: " << r.degenerate_count << "\n";
    if (mesh) {
        const auto m = claw_ai::collect_surface_mesh_ai_stats(mesh);
        oss << "Mesh: " << m.name
            << " V=" << m.vertices << " F=" << m.faces << "\n";
    }
    oss << "\n" << ai_lang::directive() << "\n"
        << "1. Is this UV result usable?\n"
        << "2. Where is distortion concentrated?\n"
        << "3. Are flipped faces acceptable?\n"
        << "4. What to try next?\n";
    return ascii_only(oss.str());
}

// --- UV View rendering ---

struct UVBBox { double umin, umax, vmin, vmax; };

UVBBox compute_uv_bbox(const std::vector<PARAM_FaceUV>& face_uvs,
                        const std::vector<int>& face_ids) {
    UVBBox b{};
    bool first = true;
    for (int fid : face_ids) {
        if (fid < 0 || fid >= (int)face_uvs.size()) continue;
        const auto& f = face_uvs[fid];
        for (int i = 0; i < 3; ++i) {
            if (first) {
                b.umin = b.umax = f.uv[i].u;
                b.vmin = b.vmax = f.uv[i].v;
                first = false;
            } else {
                if (f.uv[i].u < b.umin) b.umin = f.uv[i].u;
                if (f.uv[i].u > b.umax) b.umax = f.uv[i].u;
                if (f.uv[i].v < b.vmin) b.vmin = f.uv[i].v;
                if (f.uv[i].v > b.vmax) b.vmax = f.uv[i].v;
            }
        }
    }
    return b;
}

// Draw a single UV triangle. uv_to_screen maps UV coords -> ImGui screen coords.
void draw_uv_triangle(ImDrawList* dl,
                      const PARAM_FaceUV& fu,
                      const std::function<ImVec2(double,double)>& uv_to_screen,
                      bool filled, bool highlighted,
                      ImU32 fill_color, ImU32 edge_color, ImU32 hl_color) {
    ImVec2 s[3];
    for (int i = 0; i < 3; ++i)
        s[i] = uv_to_screen(fu.uv[i].u, fu.uv[i].v);

    if (filled && !fu.flipped) {
        dl->AddTriangleFilled(s[0], s[1], s[2],
            highlighted ? hl_color : fill_color);
    }
    // Skip wireframe when edges are disabled (alpha == 0); avoids doubling
    // draw-call count for large meshes with no visual benefit.
    if ((edge_color & IM_COL32_A_MASK) != 0)
        dl->AddTriangle(s[0], s[1], s[2], edge_color, 1.0f);
}

void render_uv_view(const char* title, ParameterizationState& s, bool& open,
                    easy3d::SurfaceMesh* mesh) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &open,
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End(); return;
    }

    const auto& face_uvs = ((s.arap_live_active ||
                              s.arap_playback_frame >= 0) &&
                              !s.arap_playback_face_uvs.empty())
        ? s.arap_playback_face_uvs
        : s.last_result.face_uvs;
    if (face_uvs.empty() || s.last_result.error_code != PARAM_ERR_None) {
        ImGui::TextDisabled("No UV data yet. Run LSCM or ARAP first.");
        ImGui::End();
        return;
    }

    // Build face id list (all faces, or only revealed faces during animation).
    std::vector<int> visible_faces;
    if (s.reveal_phase >= PARAM_REVEAL_Complete) {
        visible_faces.reserve(face_uvs.size());
        for (int i = 0; i < (int)face_uvs.size(); ++i) visible_faces.push_back(i);
    } else if (s.reveal_phase >= PARAM_REVEAL_Filling && !s.reveal_sorted_faces.empty()) {
        const int cnt = std::min(s.reveal_face_count, (int)s.reveal_sorted_faces.size());
        visible_faces.assign(s.reveal_sorted_faces.begin(),
                             s.reveal_sorted_faces.begin() + cnt);
    } else {
        visible_faces.clear(); // idle or boundary-only; no faces yet.
    }

    // Full face list for bbox computation (stable viewport framing).
    std::vector<int> all_face_ids(face_uvs.size());
    for (int i = 0; i < (int)face_uvs.size(); ++i) all_face_ids[i] = i;

    // Bounding box of ALL UV faces (not just visible) so the viewport
    // frame is stable and centered from the start.
    auto bbox = compute_uv_bbox(face_uvs, all_face_ids);
    if (bbox.umax <= bbox.umin) { bbox.umin = -1; bbox.umax = 1; bbox.vmin = -1; bbox.vmax = 1; }

    // ---- Fixed header ----
    if (ImGui::Button("Fit")) { s.uv_need_fit = true; }
    claw_ui::same_line_if_fits_button("Reset");
    if (ImGui::Button("Reset")) {
        s.uv_pan_x = 0; s.uv_pan_y = 0; s.uv_zoom = 1.0f;
        s.uv_need_fit = true;
    }
    claw_ui::same_line_if_fits_text("zoom=00.00");
    ImGui::TextDisabled("zoom=%.2f", s.uv_zoom);

    // ---- Canvas (reserve bottom ~3 lines for footer; right strip for colorbar) ----
    float reserved_h = ImGui::GetFrameHeightWithSpacing() * 3.5f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool draw_colorbar = !s.arap_live_active && s.uv_show_heatmap &&
                               s.reveal_phase >= PARAM_REVEAL_Distortion;
    const float colorbar_w = draw_colorbar ? 78.0f : 0.0f;
    ImVec2 canvas_sz(std::max(avail.x - colorbar_w, 50.0f),
                     std::max(avail.y - reserved_h, 100.0f));
    if (canvas_sz.x < 50) canvas_sz.x = 400;
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("uvcanvas", canvas_sz,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);

    const bool hovered = ImGui::IsItemHovered();
    ImVec2 mouse_uv(0, 0);

    // Auto-fit: compute zoom & pan to fit bbox.
    const double margin = 0.05;
    const double uv_w = bbox.umax - bbox.umin;
    const double uv_h = bbox.vmax - bbox.vmin;
    const double uv_cx = (bbox.umin + bbox.umax) * 0.5;
    const double uv_cy = (bbox.vmin + bbox.vmax) * 0.5;

    // Only override zoom/pan on first frame or fit request.
    if (s.uv_need_fit) {
        s.uv_need_fit = false;
        if (uv_w > 1e-10 && uv_h > 1e-10) {
            const double ar_canvas = (double)canvas_sz.x / (double)canvas_sz.y;
            const double ar_uv    = uv_w / uv_h;
            double scale;
            if (ar_uv > ar_canvas) scale = (double)canvas_sz.x * (1.0 - 2*margin) / uv_w;
            else                    scale = (double)canvas_sz.y * (1.0 - 2*margin) / uv_h;
            s.uv_zoom  = (float)scale;
            s.uv_pan_x = (float)(-uv_cx * scale + canvas_sz.x * 0.5);
            s.uv_pan_y = (float)(uv_cy * scale + canvas_sz.y * 0.5);
        }
    }

    // Pan (right/middle drag).
    if (hovered) {
        auto& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            s.uv_pan_x += io.MouseDelta.x;
            s.uv_pan_y += io.MouseDelta.y;
        }
        // Zoom (scroll). No hard upper cap: user must be able to zoom into
        // a single triangle for diagnosis. Lower cap keeps the layout visible.
        if (io.MouseWheel != 0.0f) {
            float mx = io.MousePos.x - canvas_pos.x;
            float my = io.MousePos.y - canvas_pos.y;
            float old_zoom = s.uv_zoom;
            s.uv_zoom *= (io.MouseWheel > 0) ? 1.20f : 0.83f;
            if (s.uv_zoom < 0.001f) s.uv_zoom = 0.001f;
            if (s.uv_zoom > 1.0e7f) s.uv_zoom = 1.0e7f;
            s.uv_pan_x = mx + (s.uv_pan_x - mx) * s.uv_zoom / old_zoom;
            s.uv_pan_y = my + (s.uv_pan_y - my) * s.uv_zoom / old_zoom;
        }
    }

    // UV-to-screen transform.
    auto uv_to_screen = [&](double u, double v) -> ImVec2 {
        return ImVec2(
            canvas_pos.x + (float)(u * s.uv_zoom + s.uv_pan_x),
            canvas_pos.y + (float)(-v * s.uv_zoom + s.uv_pan_y));
    };

    // Screen-to-UV.
    auto screen_to_uv = [&](float sx, float sy) -> std::pair<double, double> {
        double u = (double)((sx - canvas_pos.x - s.uv_pan_x) / s.uv_zoom);
        double v = -(double)((sy - canvas_pos.y - s.uv_pan_y) / s.uv_zoom);
        return {u, v};
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw UV triangles.
    const bool show_heatmap = !s.arap_live_active && s.uv_show_heatmap &&
                               s.reveal_phase >= PARAM_REVEAL_Distortion;
    const bool show_edges = s.uv_show_edges &&
                             s.reveal_phase >= PARAM_REVEAL_Edges;

    // Find hovered face.
    s.uv_hovered_face = -1;
    if (hovered && s.reveal_phase >= PARAM_REVEAL_Complete) {
        auto muv = screen_to_uv(ImGui::GetIO().MousePos.x,
                                 ImGui::GetIO().MousePos.y);
        double mu = muv.first, mv = muv.second;
        mouse_uv = ImVec2((float)mu, (float)mv);
        // Linear search with bounding-box early-out.
        float best_depth = 1e30f;
        for (int fid : visible_faces) {
            const auto& fu = face_uvs[fid];
            double f_umin = std::min({fu.uv[0].u, fu.uv[1].u, fu.uv[2].u});
            double f_umax = std::max({fu.uv[0].u, fu.uv[1].u, fu.uv[2].u});
            double f_vmin = std::min({fu.uv[0].v, fu.uv[1].v, fu.uv[2].v});
            double f_vmax = std::max({fu.uv[0].v, fu.uv[1].v, fu.uv[2].v});
            if (mu < f_umin - 0.01 || mu > f_umax + 0.01 ||
                mv < f_vmin - 0.01 || mv > f_vmax + 0.01) continue;
            if (point_in_uv_triangle(mu, mv,
                    fu.uv[0].u, fu.uv[0].v,
                    fu.uv[1].u, fu.uv[1].v,
                    fu.uv[2].u, fu.uv[2].v)) {
                // Use UV centroid Z for depth ordering.
                float cz = (float)((fu.uv[0].u + fu.uv[1].u + fu.uv[2].u) / 3.0);
                if (cz < best_depth) { best_depth = cz; s.uv_hovered_face = fid; }
            }
        }
    }

    // Draw all visible faces.
    for (int fid : visible_faces) {
        const auto& fu = face_uvs[fid];
        ImU32 fill_c = IM_COL32(180, 180, 200, 200); // default gray-blue
        if (show_heatmap) {
            double t = distortion_t(fu.area_distortion);
            fill_c = distortion_color(t);
        }
        ImU32 edge_c = show_edges
            ? (fu.flipped ? IM_COL32(255, 60, 60, 255) : IM_COL32(0, 0, 0, 80))
            : IM_COL32(0, 0, 0, 0);
        bool hl = (fid == s.uv_hovered_face ||
                   (s.uv_picked_face >= 0 && fid == s.uv_picked_face));
        ImU32 hl_c = (fid == s.uv_picked_face)
            ? IM_COL32(255, 255, 80, 220) : IM_COL32(200, 200, 255, 220);
        draw_uv_triangle(dl, fu, uv_to_screen, true, hl, fill_c, edge_c, hl_c);
    }

    // Draw UV boundary outline.
    if (s.uv_show_boundary && s.reveal_phase >= PARAM_REVEAL_UVOutline) {
        // Use all faces so the outline is visible before the staged
        // interior-fill animation starts.
        const auto& boundary_faces = all_face_ids;
        using QuantizedUV = std::pair<long long, long long>;
        using QuantizedEdge = std::pair<QuantizedUV, QuantizedUV>;
        struct UVEdgeRecord {
            int count = 0;
            PARAM_UV a{};
            PARAM_UV b{};
        };
        auto quantize_uv = [](const PARAM_UV& uv) -> QuantizedUV {
            return {
                (long long)std::llround(uv.u * 1000000000.0),
                (long long)std::llround(uv.v * 1000000000.0)
            };
        };
        auto edge_key = [&](const PARAM_UV& a, const PARAM_UV& b) -> QuantizedEdge {
            QuantizedUV qa = quantize_uv(a);
            QuantizedUV qb = quantize_uv(b);
            if (qb < qa)
                std::swap(qa, qb);
            return {qa, qb};
        };
        std::map<QuantizedEdge, UVEdgeRecord> edge_count;
        for (int fid : boundary_faces) {
            const auto& fu = face_uvs[fid];
            for (int i = 0; i < 3; ++i) {
                const PARAM_UV a = fu.uv[i];
                const PARAM_UV b = fu.uv[(i + 1) % 3];
                auto& rec = edge_count[edge_key(a, b)];
                if (rec.count == 0) {
                    rec.a = a;
                    rec.b = b;
                }
                ++rec.count;
            }
        }
        for (const auto& ec : edge_count) {
            const auto& rec = ec.second;
            if (rec.count != 1) continue;
            auto p0 = uv_to_screen(rec.a.u, rec.a.v);
            auto p1 = uv_to_screen(rec.b.u, rec.b.v);
            dl->AddLine(p0, p1, IM_COL32(0, 220, 240, 255), 2.0f);
        }
    }

    // Draw picked point marker. Bright cyan -- distinct from the yellow
    // picked-face highlight, the 3D lime overlay marker, and the heatmap
    // colors (no triangle ever paints in cyan).
    if (s.uv_picked_face >= 0 && s.uv_picked_face < (int)face_uvs.size()) {
        auto pp = uv_to_screen(s.uv_picked_u, s.uv_picked_v);
        dl->AddCircleFilled(pp, 7.0f, IM_COL32(0, 230, 255, 255)); // cyan
        dl->AddCircle(pp, 10.0f, IM_COL32(0, 0, 0, 255), 0, 2.0f);
        dl->AddCircle(pp, 13.0f, IM_COL32(0, 230, 255, 200), 0, 1.5f);
    }

    // Draw hovered face outline.
    if (s.uv_hovered_face >= 0 && s.uv_hovered_face < (int)face_uvs.size()) {
        const auto& fu = face_uvs[s.uv_hovered_face];
        for (int i = 0; i < 3; ++i) {
            auto p0 = uv_to_screen(fu.uv[i].u, fu.uv[i].v);
            auto p1 = uv_to_screen(fu.uv[(i+1)%3].u, fu.uv[(i+1)%3].v);
            dl->AddLine(p0, p1, IM_COL32(255, 255, 255, 180), 2.0f);
        }
        auto muv = screen_to_uv(ImGui::GetIO().MousePos.x,
                                 ImGui::GetIO().MousePos.y);
        double mu = muv.first, mv = muv.second;
        auto mp = uv_to_screen(mu, mv);
        dl->AddCircleFilled(mp, 3.0f, IM_COL32(255, 255, 200, 200));
    }

    // Click to pick.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        s.reveal_phase >= PARAM_REVEAL_Complete) {
        s.uv_picked_face = s.uv_hovered_face;
        if (s.uv_picked_face >= 0) {
            auto muv2 = screen_to_uv(ImGui::GetIO().MousePos.x,
                                      ImGui::GetIO().MousePos.y);
            s.uv_picked_u = (float)muv2.first;
            s.uv_picked_v = (float)muv2.second;
            // Project the UV pick back to a 3D position + highlight face.
            if (mesh) apply_uv_pick(s, mesh);
        }
    }

    // ---- Color bar (CloudCompare-style legend) ----
    // Drawn directly into the strip reserved to the right of the canvas.
    // Maps area_distortion [0, 2+] -> distortion_color() gradient.
    if (draw_colorbar) {
        const float bar_x = canvas_pos.x + canvas_sz.x + 16.0f;
        const float bar_y = canvas_pos.y + 18.0f;
        const float bar_w = 18.0f;
        const float bar_h = std::max(canvas_sz.y - 36.0f, 80.0f);

        // Gradient: top = highest distortion (t=1), bottom = lowest (t=0).
        const int N = 64;
        for (int i = 0; i < N; ++i) {
            float t0 = 1.0f - (float)i / (float)N;
            float t1 = 1.0f - (float)(i + 1) / (float)N;
            ImU32 c0 = distortion_color(t0);
            ImU32 c1 = distortion_color(t1);
            ImVec2 p0(bar_x, bar_y + bar_h * (float)i / (float)N);
            ImVec2 p1(bar_x + bar_w, bar_y + bar_h * (float)(i + 1) / (float)N);
            dl->AddRectFilledMultiColor(p0, p1, c0, c0, c1, c1);
        }
        // Border.
        dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                    IM_COL32(220, 220, 220, 255), 0.0f, 0, 1.0f);

        // Title.
        dl->AddText(ImVec2(bar_x - 4.0f, bar_y - 16.0f),
                    IM_COL32(230, 230, 230, 255), "Area");
        dl->AddText(ImVec2(bar_x - 4.0f, bar_y - 4.0f),
                    IM_COL32(180, 180, 180, 255), "Dist.");

        // Tick labels at t = 0, 0.25, 0.5, 0.75, 1.0 -> area_distortion 0, 0.5, 1.0, 1.5, 2.0.
        const char* tlabels[5] = {"0.0", "0.5", "1.0", "1.5", "2.0+"};
        for (int k = 0; k < 5; ++k) {
            float ty = bar_y + bar_h * (float)k / 4.0f;  // k=0 top (max), k=4 bottom (0)
            // Tick mark.
            dl->AddLine(ImVec2(bar_x + bar_w, ty),
                        ImVec2(bar_x + bar_w + 4.0f, ty),
                        IM_COL32(220, 220, 220, 255), 1.0f);
            // Label: top (k=0) shows "2.0+" because top = max distortion.
            dl->AddText(ImVec2(bar_x + bar_w + 6.0f, ty - 7.0f),
                        IM_COL32(220, 220, 220, 255),
                        tlabels[4 - k]);
        }

        // Highlight the actual max distortion line on the bar.
        if (s.last_result_valid) {
            double max_d = s.last_result.max_area_distortion;
            float tmax = (float)std::min(1.0, max_d / 2.0);
            float my = bar_y + bar_h * (1.0f - tmax);
            dl->AddLine(ImVec2(bar_x - 4.0f, my),
                        ImVec2(bar_x + bar_w + 4.0f, my),
                        IM_COL32(255, 255, 255, 220), 1.5f);
            char mlabel[32];
            std::snprintf(mlabel, sizeof(mlabel), "max %.2f", max_d);
            dl->AddText(ImVec2(bar_x - 6.0f, bar_y + bar_h + 4.0f),
                        IM_COL32(255, 255, 255, 220), mlabel);
        }
    }

    // ---- Footer ----
    ImGui::TextDisabled("Faces: %d | UV: [%.2f, %.2f] x [%.2f, %.2f]",
        (int)visible_faces.size(), bbox.umin, bbox.umax, bbox.vmin, bbox.vmax);
    if (s.uv_picked_face >= 0) {
        claw_ui::same_line_if_fits_width(180.0f);
        ImGui::Text("Pick f=%d (%.3f, %.3f)",
            s.uv_picked_face, s.uv_picked_u, s.uv_picked_v);
    }
    ImGui::Checkbox("Heatmap", &s.uv_show_heatmap);
    claw_ui::same_line_if_fits_text("Edges");
    ImGui::Checkbox("Edges", &s.uv_show_edges);
    claw_ui::same_line_if_fits_text("Boundary");
    ImGui::Checkbox("Boundary", &s.uv_show_boundary);

    ImGui::End();
}

// --- Staged reveal state machine ---

void advance_reveal(ParameterizationState& s) {
    const double now = ImGui::GetTime();
    const double elapsed = now - s.reveal_start_time;

    switch (s.reveal_phase) {
    case PARAM_REVEAL_Idle:
        break;

    case PARAM_REVEAL_Boundary:
        // Show the 3D boundary briefly, then move to the UV outline.
        if (elapsed > 1.0) {
            s.reveal_phase = PARAM_REVEAL_UVOutline;
            s.reveal_start_time = now;
        }
        break;

    case PARAM_REVEAL_UVOutline:
        // Show the UV boundary briefly before filling faces.
        if (elapsed > 0.5) {
            s.reveal_phase = PARAM_REVEAL_Filling;
            s.reveal_start_time = now;
            s.reveal_face_count = 0;
            // Sort faces by UV center v coordinate (descending = top to bottom).
            if (!s.reveal_sorted_faces.empty()) {
                const auto& fuvs = s.last_result.face_uvs;
                std::sort(s.reveal_sorted_faces.begin(), s.reveal_sorted_faces.end(),
                    [&](int a, int b) {
                        double va = (fuvs[a].uv[0].v + fuvs[a].uv[1].v + fuvs[a].uv[2].v) / 3.0;
                        double vb = (fuvs[b].uv[0].v + fuvs[b].uv[1].v + fuvs[b].uv[2].v) / 3.0;
                        return va > vb; // top to bottom
                    });
            }
        }
        break;

    case PARAM_REVEAL_Filling:
        // Reveal faces in batches of about 5% per frame at 60fps.
        {
            const int total = (int)s.reveal_sorted_faces.size();
            const int per_frame = std::max(1, total / 40); // ~0.7s total
            s.reveal_face_count = std::min(total, s.reveal_face_count + per_frame);
            if (s.reveal_face_count >= total) {
                s.reveal_phase = PARAM_REVEAL_Edges;
                s.reveal_start_time = now;
            }
        }
        break;

    case PARAM_REVEAL_Edges:
        // Keep edges visible briefly before showing distortion.
        if (elapsed > 0.3) {
            s.reveal_phase = PARAM_REVEAL_Distortion;
            s.reveal_start_time = now;
        }
        break;

    case PARAM_REVEAL_Distortion:
        // Fade in the distortion heatmap.
        if (elapsed > 0.5) {
            s.reveal_phase = PARAM_REVEAL_Complete;
            s.reveal_start_time = now;
        }
        break;

    case PARAM_REVEAL_Complete:
        break;
    }
}

} // namespace

void renderDialogParameterization(ViewportCanvas* viewer, ParameterizationState& s,
                                    bool& open) {
    prepare_dialog_window(560, 620);
    DIALOG_BODY("Parameterization / UV", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        const bool busy = s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::Parameterization);
        if (!open && busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            if (win)
                win->algorithm_controller().request_cancel();
            claw3d::app::wake_event_loop();
        }

        static bool uv_view_open = false;
        static bool show_3d_heatmap = false;

        claw_ui::same_line_right_if_fits_button("?");
        if (ImGui::SmallButton("?")) {
            send_ai_prompt(win, PARAM_HELP_PROMPT,
                           "Ask AI: UV Parameterization help");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ask AI about LSCM UV Parameterization");

        ImGui::Spacing();
        auto* viewer = win ? win->viewer() : nullptr;
        auto lock_param_pick_input = [&]() {
            if (viewer) {
                viewer->input_locked_ = true;
                s.owns_viewport_input_lock = true;
            }
        };
        auto release_param_pick_input = [&]() {
            if (viewer && s.owns_viewport_input_lock) {
                viewer->input_locked_ = false;
                s.owns_viewport_input_lock = false;
            }
        };
        if (!open) {
            s.pick_mode = 0;
            s.seam_pick_mode = 0;
            release_param_pick_input();
        }

        easy3d::SurfaceMesh* mesh = nullptr;
        if (viewer) {
            mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer->current_model());
            if (!mesh) {
                for (auto& mp : viewer->models()) {
                    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(mp.get())) {
                        mesh = sm; break;
                    }
                }
            }
        }

        if (!mesh) {
            s.pick_mode = 0;
            s.seam_pick_mode = 0;
            release_param_pick_input();
            ImGui::TextColored(claw_ui::status_error_color(),
                "No surface mesh loaded.");
            if (ImGui::Button("Close")) open = false;
        } else {
            const int nv = (int)mesh->n_vertices();
            const int nf = (int)mesh->n_faces();
            const bool is_triangle = mesh->is_triangle_mesh();
            const bool is_closed = mesh->is_closed();
            if ((s.pick_mode != 0 || s.seam_pick_mode != 0) && viewer && !busy)
                lock_param_pick_input();
            else if (s.owns_viewport_input_lock)
                release_param_pick_input();

            ImGui::SeparatorText("Input");
            ImGui::TextColored(claw_ui::status_muted_color(),
                "%s (v=%d f=%d) triangle=%s closed=%s",
                mesh->name().c_str(), nv, nf,
                is_triangle ? "yes" : "no",
                is_closed ? "yes" : "no");

            if (!is_triangle) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Triangle mesh required for UV parameterization.");
            }
            if (is_closed) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Closed mesh requires a seam before UV parameterization.");
            }

            // Seam controls for closed triangle meshes. We expose them
            // before the Run button so the user can build a seam path first.
            if (is_triangle && is_closed) {
                ImGui::SeparatorText("Seams (closed mesh)");
                if (s.seam_pick_mode == 1) {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Click a vertex on the mesh for SEAM START...");
                    if (ImGui::Button("Cancel##seam")) {
                        s.seam_pick_mode = 0;
                        release_param_pick_input();
                    }
                } else if (s.seam_pick_mode == 2) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Click a vertex on the mesh for SEAM END...");
                    if (ImGui::Button("Cancel##seam")) {
                        s.seam_pick_mode = 0;
                        release_param_pick_input();
                    }
                } else {
                    if (ImGui::Button("Pick Seam Start")) {
                        s.seam_pick_mode = 1;
                        s.seam_status[0] = 0;
                        lock_param_pick_input();
                    }
                    claw_ui::same_line_if_fits_button("Pick Seam End");
                    if (ImGui::Button("Pick Seam End")) {
                        s.seam_pick_mode = 2;
                        s.seam_status[0] = 0;
                        lock_param_pick_input();
                    }
                }
                const bool can_add = s.seam_pending_start >= 0 &&
                                      s.seam_pending_end >= 0 &&
                                      s.seam_pending_start != s.seam_pending_end;
                ImGui::BeginDisabled(!can_add);
                if (ImGui::Button("Add Seam Path")) {
                    std::vector<int> path_vids;
                    double path_len = 0.0;
                    if (shortest_path_on_mesh(mesh, s.seam_pending_start,
                                               s.seam_pending_end,
                                               path_vids, path_len) &&
                        path_vids.size() >= 2)
                    {
                        ParamSeamUIPath sp;
                        sp.start_vertex = s.seam_pending_start;
                        sp.end_vertex   = s.seam_pending_end;
                        sp.vertex_ids   = std::move(path_vids);
                        sp.length       = path_len;
                        s.seam_paths.push_back(std::move(sp));
                        s.seam_pending_start = -1;
                        s.seam_pending_end   = -1;
                        std::snprintf(s.seam_status, sizeof(s.seam_status),
                            "Added seam path with %d edges (len=%.3f). %d total.",
                            (int)s.seam_paths.back().vertex_ids.size() - 1,
                            s.seam_paths.back().length,
                            (int)s.seam_paths.size());
                        rebuild_seam_overlay(mesh, s);
                    } else {
                        std::snprintf(s.seam_status, sizeof(s.seam_status),
                            "No mesh-edge path between picked vertices.");
                    }
                }
                ImGui::EndDisabled();
                claw_ui::same_line_if_fits_button("Clear Seams");
                ImGui::BeginDisabled(s.seam_paths.empty() &&
                                      s.seam_pending_start < 0 &&
                                      s.seam_pending_end < 0);
                if (ImGui::Button("Clear Seams")) clear_seams(s, mesh);
                ImGui::EndDisabled();
                claw_ui::same_line_if_fits_text("Show Seams");
                if (ImGui::Checkbox("Show Seams", &s.show_seams))
                    rebuild_seam_overlay(mesh, s);

                double total_len = 0.0;
                for (const auto& sp : s.seam_paths) total_len += sp.length;
                ImGui::TextDisabled(
                    "Pending: start=%d end=%d | Paths: %d | Total len: %.3f",
                    s.seam_pending_start, s.seam_pending_end,
                    (int)s.seam_paths.size(), total_len);
                if (s.seam_status[0])
                    ImGui::TextColored(claw_ui::status_running_color(),
                        "%s", s.seam_status);

                // Keep overlay in sync with state every frame so that
                // reopening the dialog on a closed mesh immediately
                // reflects the (empty) state.
                rebuild_seam_overlay(mesh, s);
            } else {
                // Open mesh: hide any leftover seam drawables from a
                // previous closed-mesh session.
                rebuild_seam_overlay(mesh, s);
            }

#ifdef CLAW3D_HAS_CGAL
            // --- Algorithm ---
            ImGui::SeparatorText("Algorithm");
            {
                const char* algo_items[] = {"LSCM", "ARAP"};
                ImGui::Combo("Method", &s.algorithm, algo_items, 2);
            }
            if (s.algorithm == PARAM_ALGO_ARAP) {
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt("Iterations", &s.arap_iterations, 1, 10);
                if (s.arap_iterations < 1) s.arap_iterations = 1;
                if (s.arap_iterations > 500) s.arap_iterations = 500;
                ImGui::SetNextItemWidth(200);
                ImGui::InputDouble("Tolerance", &s.arap_tolerance, 1e-7, 1e-5,
                                   "%.1e");
                if (s.arap_tolerance < 1e-15) s.arap_tolerance = 1e-15;
                ImGui::SetNextItemWidth(200);
                ImGui::InputDouble("Lambda", &s.arap_lambda, 10.0, 100.0, "%.1f");
                if (s.arap_lambda < 0.0) s.arap_lambda = 0.0;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "0=ASAP (angle only)\n"
                        ">0=ARAP (shape + angle)\n"
                        "1000=default, higher=more rigid");
                ImGui::Checkbox("Show process", &s.show_process);
                claw_ui::same_line_if_fits_width(120.0f);
                ImGui::BeginDisabled(!s.show_process);
                const char* speed_items[] = {"Normal", "Slow"};
                ImGui::SetNextItemWidth(120);
                ImGui::Combo("Speed", &s.process_speed, speed_items, 2);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Normal: live snapshots with a short display pause.\n"
                        "Slow: intentionally slows each ARAP iteration so the "
                        "optimization is easier to inspect.");
                if (is_closed && s.show_process)
                    ImGui::TextDisabled(
                        "Live ARAP process is currently shown for open meshes; "
                        "closed/seam meshes show the final ARAP UV.");
            } else {
                ImGui::TextWrapped(
                    "LSCM is angle-preserving. Single solve, no iterations.");
            }

            // --- Run / Cancel ---
            if (!busy) {
                ImGui::Spacing();
                if (ImGui::Button("AI Parameter Advice"))
                    send_ai_prompt(win, build_param_advice_prompt(mesh),
                                   "Ask AI: LSCM parameter advice");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }
            ImGui::Spacing();
            if (!busy) {
                const bool can_run = is_triangle &&
                    (!is_closed || !s.seam_paths.empty());
                ImGui::BeginDisabled(!can_run);
                if (ImGui::Button(s.algorithm == PARAM_ALGO_ARAP
                        ? "Run ARAP" : "Run LSCM")) {
                    // Clear previous 3D heatmap unconditionally -- covers the
                    // case where the user toggled heatmap off without an
                    // explicit Run LSCM in between.
                    {
                        auto* fd = mesh->renderer()->get_triangles_drawable("faces");
                        if (fd) {
                            fd->set_uniform_coloring(
                                easy3d::vec4(0.6f, 0.6f, 0.6f, 1.0f));
                            fd->update();
                        }
                        auto oldd = mesh->get_vertex_property<float>(
                            "v:param_distortion");
                        if (oldd) mesh->remove_vertex_property(oldd);
                        auto oldc = mesh->get_vertex_property<easy3d::vec3>(
                            "v:param_color");
                        if (oldc) mesh->remove_vertex_property(oldc);
                        mesh->renderer()->update();
                        show_3d_heatmap = false;
                    }

                    PARAM_Config cfg;
                    cfg.algorithm       = s.algorithm;
                    cfg.arap_iterations = s.arap_iterations;
                    cfg.arap_tolerance  = s.arap_tolerance;
                    cfg.arap_lambda     = s.arap_lambda;
                    cfg.show_process    = s.show_process;
                    cfg.arap_snapshot_stride = 1;
                    cfg.arap_snapshot_delay_ms = s.show_process
                        ? (s.process_speed == 1 ? 220 : 60)
                        : 0;

                    std::vector<PARAM_SeamPath> seam_paths;
                    if (is_closed && !s.seam_paths.empty()) {
                        seam_paths.reserve(s.seam_paths.size());
                        for (const auto& sp : s.seam_paths) {
                            PARAM_SeamPath r;
                            r.start_vertex = sp.start_vertex;
                            r.end_vertex   = sp.end_vertex;
                            r.vertex_ids   = sp.vertex_ids;
                            r.length       = sp.length;
                            seam_paths.push_back(std::move(r));
                        }
                    }

                    s.last_result_valid = false;
                    s.close_requested = false;
                    s.last_error.clear();
                    s.final_result_ready.store(false,
                        std::memory_order_release);

                    // Reset reveal + pick + playback state.
                    s.reveal_phase = PARAM_REVEAL_Idle;
                    s.arap_playback_frame = -1;
                    s.arap_playback_face_uvs.clear();
                    s.arap_live_active = false;
                    s.arap_live_iteration = -1;
                    s.arap_live_energy = 0.0;
                    s.arap_live_snapshot_count = 0;
                    s.reveal_progress = 0.0f;
                    s.reveal_sorted_faces.clear();
                    s.reveal_face_count = 0;
                    s.uv_need_fit = true;
                    clear_pick(s, mesh);

                    claw3d::services::ParameterizationJobStart request;
                    request.source_mesh = mesh;
                    request.source_handle =
                        (win && win->viewer())
                            ? win->viewer()->model_handle(mesh)
                            : ModelHandle{};
                    request.config = cfg;
                    request.seam_paths = std::move(seam_paths);
                    request.final_result_ready = &s.final_result_ready;
                    request.wake_ui = []() { claw3d::app::wake_event_loop(); };

                    if (win) {
                        s.runner =
                            claw3d::services::start_parameterization_job(
                                win->algorithm_controller(), request);
                    } else {
                        s.runner.reset();
                    }
                    if (!s.runner) {
                        s.final_result_ready.store(true,
                            std::memory_order_release);
                        s.last_error = "Failed to start parameterization job.";
                        LOG(WARNING) << s.last_error;
                    }
                }
                ImGui::EndDisabled();
            } else {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Running %s...",
                    s.algorithm == PARAM_ALGO_ARAP ? "ARAP" : "LSCM");
                if (ImGui::Button("Cancel")) {
                    s.runner.cancel();
                    if (win)
                        win->algorithm_controller().request_cancel();
                    claw3d::app::wake_event_loop();
                }
                if (s.runner && s.runner.is_cancelled()) {
                    claw_ui::same_line_if_fits_text("Cancel requested...");
                    ImGui::TextDisabled("Cancel requested...");
                }
            }

            // ARAP live process: consume snapshots while the worker is still
            // running. This makes the UV optimization visible as it happens
            // instead of only replaying captured frames after completion.
            if (busy && s.runner && s.algorithm == PARAM_ALGO_ARAP &&
                s.show_process && !is_closed)
            {
                PARAM_Result live;
                s.runner.get_result(live);
                if (live.error_code == PARAM_ERR_None &&
                    !live.face_uvs.empty() &&
                    !live.arap_snapshots.empty())
                {
                    const int latest = (int)live.arap_snapshots.size() - 1;
                    if (latest != s.arap_playback_frame ||
                        s.arap_playback_face_uvs.empty())
                    {
                        if (build_arap_snapshot_face_uvs(
                                live, latest, s.arap_playback_face_uvs))
                        {
                            s.last_result = live;
                            s.last_result_valid = true;
                            s.arap_playback_frame = latest;
                            const auto& snap = live.arap_snapshots[latest];
                            s.arap_live_active = true;
                            s.arap_live_iteration = snap.iteration;
                            s.arap_live_energy = snap.energy;
                            s.arap_live_snapshot_count =
                                (int)live.arap_snapshots.size();
                            s.reveal_phase = PARAM_REVEAL_Complete;
                            s.reveal_face_count = (int)live.face_uvs.size();
                            if (s.reveal_sorted_faces.empty()) {
                                for (int i = 0; i < (int)live.face_uvs.size(); ++i)
                                    s.reveal_sorted_faces.push_back(i);
                            }
                            uv_view_open = true;
                        }
                    }
                    ImGui::TextColored(claw_ui::status_running_color(),
                        "Live ARAP: iter %d | E=%.6g | snapshots=%d",
                        s.arap_live_iteration, s.arap_live_energy,
                        s.arap_live_snapshot_count);
                    if (uv_view_open)
                        render_uv_view("UV View", s, uv_view_open, mesh);
                    claw3d::app::wake_event_loop();
                }
            }

            // Worker completion -> handoff + start staged reveal.
            if (s.runner && busy && s.runner.is_done() &&
                s.final_result_ready.load(std::memory_order_acquire))
            {
                if (s.runner.has_error())
                    s.last_error = s.runner.last_error();
                s.runner.get_result(s.last_result);
                s.last_result_valid = true;
                if (s.last_result.error_code == PARAM_ERR_None) {
                    // Set up reveal faces.
                    s.reveal_sorted_faces.clear();
                    for (int i = 0; i < (int)s.last_result.face_uvs.size(); ++i)
                        s.reveal_sorted_faces.push_back(i);
                    s.arap_live_active = false;
                    s.arap_playback_frame = -1;
                    s.arap_playback_face_uvs.clear();
                    if (s.algorithm == PARAM_ALGO_ARAP &&
                        s.show_process &&
                        !s.last_result.arap_snapshots.empty())
                    {
                        const int last_frame =
                            (int)s.last_result.arap_snapshots.size() - 1;
                        if (build_arap_snapshot_face_uvs(
                                s.last_result, last_frame,
                                s.arap_playback_face_uvs))
                        {
                            s.arap_playback_frame = last_frame;
                            const auto& snap =
                                s.last_result.arap_snapshots[last_frame];
                            s.arap_live_iteration = snap.iteration;
                            s.arap_live_energy = snap.energy;
                            s.arap_live_snapshot_count =
                                (int)s.last_result.arap_snapshots.size();
                        }
                    }
                    // LSCM uses the staged pipeline reveal. ARAP has already
                    // shown its optimization live, so final handoff should stay
                    // on the final UV layout instead of replaying a second,
                    // artificial staged animation.
                    if (s.algorithm == PARAM_ALGO_ARAP) {
                        s.reveal_phase = PARAM_REVEAL_Complete;
                        s.reveal_face_count = (int)s.reveal_sorted_faces.size();
                    } else {
                        s.reveal_phase = PARAM_REVEAL_Boundary;
                        s.reveal_face_count = 0;
                    }
                    s.reveal_start_time = ImGui::GetTime();
                    uv_view_open = true;

                    // Apply per-vertex distortion + color to source mesh for 3D heatmap.
                    // Use vec3 v:param_color (set_property_coloring) instead of a
                    // scalar field, so the 3D mesh and the UV view share the same
                    // blue->green->yellow->red gradient (one legend covers both).
                    auto* fd = mesh->renderer()->get_triangles_drawable("faces");
                    if (fd) {
                        auto oldd = mesh->get_vertex_property<float>(
                            "v:param_distortion");
                        if (oldd) mesh->remove_vertex_property(oldd);
                        auto oldc = mesh->get_vertex_property<easy3d::vec3>(
                            "v:param_color");
                        if (oldc) mesh->remove_vertex_property(oldc);
                        auto dprop = mesh->vertex_property<float>(
                            "v:param_distortion", 0.0f);
                        auto cprop = mesh->vertex_property<easy3d::vec3>(
                            "v:param_color", easy3d::vec3(0.6f, 0.6f, 0.6f));
                        std::vector<int> vcount(mesh->n_vertices(), 0);
                        for (const auto& fu : s.last_result.face_uvs) {
                            if (fu.face_id < 0 || fu.face_id >= (int)mesh->n_faces())
                                continue;
                            auto f = easy3d::SurfaceMesh::Face(fu.face_id);
                            if (!f.is_valid()) continue;
                            float d = (float)fu.area_distortion;
                            for (auto v : mesh->vertices(f)) {
                                dprop[v] += d;
                                vcount[(int)v.idx()]++;
                            }
                        }
                        for (auto v : mesh->vertices()) {
                            int idx = (int)v.idx();
                            if (vcount[idx] > 0)
                                dprop[v] /= (float)vcount[idx];
                            float r, g, b;
                            distortion_color_rgb(distortion_t(dprop[v]), r, g, b);
                            cprop[v] = easy3d::vec3(r, g, b);
                        }
                        fd->set_property_coloring(easy3d::State::VERTEX,
                            "v:param_color");
                        fd->update();
                        mesh->renderer()->update();
                        show_3d_heatmap = true;
                    }
                }
                mark_algorithm_done(win);
                s.runner.reset();
            }

            // Advance reveal animation every frame.
            if (s.reveal_phase > PARAM_REVEAL_Idle &&
                s.reveal_phase < PARAM_REVEAL_Complete) {
                advance_reveal(s);
            }

            // Post-run display.
            if (s.last_result_valid && !busy) {
                const auto& r = s.last_result;
                ImGui::Spacing();
                if (r.error_code != PARAM_ERR_None) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", r.error_message);
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Result: %d faces in UV | %.0f ms",
                        (int)r.face_uvs.size(), r.elapsed_ms);
                    ImGui::TextDisabled(
                        "Boundary: %d edges | %d vertices in loop",
                        r.boundary_edge_count, (int)r.boundary_vertices.size());
                    ImGui::TextDisabled(
                        "Area distortion: mean=%.3f max=%.3f",
                        r.mean_area_distortion, r.max_area_distortion);
                    ImGui::TextDisabled(
                        "Angle distortion: mean=%.3f max=%.3f",
                        r.mean_angle_distortion, r.max_angle_distortion);
                    if (r.flipped_count > 0)
                        ImGui::TextColored(claw_ui::status_error_color(),
                            "Flipped faces: %d", r.flipped_count);
                    else
                        ImGui::TextDisabled("Flipped faces: 0");
                    if (r.degenerate_count > 0)
                        ImGui::TextDisabled("Degenerate faces: %d", r.degenerate_count);

                    // 3D heatmap toggle. The on path uses v:param_color (vec3)
                    // so the 3D view matches the UV view's gradient.
                    {
                        bool prev_hm = show_3d_heatmap;
                        ImGui::Checkbox("Show 3D Heatmap", &show_3d_heatmap);
                        if (prev_hm && !show_3d_heatmap) {
                            auto* fd2 = mesh->renderer()->get_triangles_drawable("faces");
                            if (fd2) {
                                fd2->set_uniform_coloring(
                                    easy3d::vec4(0.6f, 0.6f, 0.6f, 1.0f));
                                fd2->update();
                            }
                            mesh->renderer()->update();
                        } else if (!prev_hm && show_3d_heatmap) {
                            auto* fd2 = mesh->renderer()->get_triangles_drawable("faces");
                            if (fd2) {
                                auto cp = mesh->get_vertex_property<easy3d::vec3>(
                                    "v:param_color");
                                if (cp) {
                                    fd2->set_property_coloring(easy3d::State::VERTEX,
                                        "v:param_color");
                                    fd2->update();
                                }
                            }
                            mesh->renderer()->update();
                        }
                    }

                    // Reveal status.
                    const char* phase_names[] = {
                        "Idle", "3D Boundary", "UV Outline", "Filling...",
                        "Edges", "Distortion", "Complete"
                    };
                    ImGui::TextColored(claw_ui::status_running_color(),
                        "Reveal: %s (%d/%d faces visible)",
                        phase_names[s.reveal_phase],
                        (int)(s.reveal_phase >= PARAM_REVEAL_Complete
                            ? s.reveal_sorted_faces.size()
                            : (s.reveal_phase >= PARAM_REVEAL_Filling
                                ? s.reveal_face_count : 0)),
                        (int)s.reveal_sorted_faces.size());

                    // 3D / UV correspondence picking.
                    if (s.reveal_phase >= PARAM_REVEAL_Complete) {
                        ImGui::SeparatorText("Correspondence");
                        if (s.pick_mode == 1) {
                            ImGui::TextColored(claw_ui::status_warning_color(),
                                "Click on a 3D mesh face...");
                            if (ImGui::Button("Cancel##pick")) {
                                s.pick_mode = 0;
                                release_param_pick_input();
                            }
                        } else {
                            if (ImGui::Button("Pick 3D Point")) {
                                s.pick_mode = 1;
                                s.pick_status[0] = 0;
                                lock_param_pick_input();
                            }
                            claw_ui::same_line_if_fits_text("(or click in UV view)");
                            ImGui::TextDisabled("(or click in UV view)");
                            if (s.picked_3d_face >= 0 || s.uv_picked_face >= 0) {
                                if (ImGui::Button("Clear##pick"))
                                    clear_pick(s, mesh);
                            }
                        }
                        if (s.pick_status[0])
                            ImGui::TextColored(claw_ui::status_running_color(),
                                "%s", s.pick_status);
                    }

                    // AI + UV View.
                    if (ImGui::Button("AI Evaluate Result"))
                        send_ai_prompt(win, build_param_evaluation_prompt(s, mesh),
                                       "Ask AI: evaluate UV parameterization");
                    claw_ui::same_line_if_fits_button("Toggle UV View");
                    if (ImGui::Button("Toggle UV View")) uv_view_open = !uv_view_open;
                    claw_ui::same_line_if_fits_button("Fit UV View");
                    if (ImGui::Button("Fit UV View")) s.uv_need_fit = true;
                    if (s.algorithm == PARAM_ALGO_ARAP) {
                        ImGui::Spacing();
                        ImGui::TextDisabled(
                            "ARAP live snapshots captured during run: %d",
                            (int)r.arap_snapshots.size());
                        if (!r.arap_snapshots.empty()) {
                            const auto& last = r.arap_snapshots.back();
                            claw_ui::same_line_if_fits_text("last iter=000 E=0.000000");
                            ImGui::TextDisabled("last iter=%d E=%.6g",
                                last.iteration, last.energy);
                        }
                    }

                    if (uv_view_open)
                        render_uv_view("UV View", s, uv_view_open, mesh);
                }
            }

            // Handle 3D viewport seam-vertex pick. Uses the same picker
            // workflow as the picker, then snaps to the nearest face-corner vertex.
            if (is_triangle && is_closed && s.seam_pick_mode != 0 &&
                !busy && viewer)
            {
                auto& io = ImGui::GetIO();
                const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                const bool in_vp = viewer->is_in_viewport(io.MousePos.x, io.MousePos.y);
                if (clicked && in_vp) {
                    const float local_x = io.MousePos.x - viewer->viewport_min_x();
                    const float local_y = io.MousePos.y - viewer->viewport_min_y();
                    const int vp_w = (int)(viewer->viewport_max_x() -
                                           viewer->viewport_min_x());
                    const int vp_h = (int)(viewer->viewport_max_y() -
                                           viewer->viewport_min_y());
                    int picked_vid = -1;
                    viewer->run_with_panel_gl_viewport(vp_w, vp_h, [&]() {
                        easy3d::SurfaceMeshPicker picker(viewer->camera());
                        auto face = picker.pick_face(mesh, (int)local_x, (int)local_y);
                        if (face.is_valid()) {
                            auto pt = picker.picked_point(mesh, face,
                                                           (int)local_x, (int)local_y);
                            picked_vid = nearest_face_vertex(mesh, face, pt);
                        }
                    });
                    if (picked_vid >= 0) {
                        if (s.seam_pick_mode == 1) {
                            s.seam_pending_start = picked_vid;
                            std::snprintf(s.seam_status, sizeof(s.seam_status),
                                "Seam start = vertex %d", picked_vid);
                        } else {
                            s.seam_pending_end = picked_vid;
                            std::snprintf(s.seam_status, sizeof(s.seam_status),
                                "Seam end = vertex %d", picked_vid);
                        }
                        s.seam_pick_mode = 0;
                        release_param_pick_input();
                        rebuild_seam_overlay(mesh, s);
                    } else {
                        std::snprintf(s.seam_status, sizeof(s.seam_status),
                            "No vertex under cursor -- click on the mesh");
                    }
                }
            }

            // Handle 3D viewport pick click (one-shot, when pick_mode==1).
            // ViewportCanvas now keeps the Camera screen in sync with the
            // FBO, so we just need to match the GL viewport during picking.
            // ViewportCanvas owns that OpenGL scope so this UI panel does not
            // depend on OpenGL headers.
            if (s.last_result_valid &&
                s.last_result.error_code == PARAM_ERR_None &&
                s.pick_mode == 1 && !busy && viewer)
            {
                auto& io = ImGui::GetIO();
                const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                const bool in_vp = viewer->is_in_viewport(io.MousePos.x, io.MousePos.y);
                if (clicked && in_vp) {
                    const float local_x = io.MousePos.x - viewer->viewport_min_x();
                    const float local_y = io.MousePos.y - viewer->viewport_min_y();
                    const int vp_w = (int)(viewer->viewport_max_x() -
                                           viewer->viewport_min_x());
                    const int vp_h = (int)(viewer->viewport_max_y() -
                                           viewer->viewport_min_y());
                    bool picked_face = false;
                    viewer->run_with_panel_gl_viewport(vp_w, vp_h, [&]() {
                        easy3d::SurfaceMeshPicker picker(viewer->camera());
                        auto face = picker.pick_face(mesh, (int)local_x, (int)local_y);
                        if (face.is_valid()) {
                            auto pt = picker.picked_point(mesh, face,
                                                           (int)local_x, (int)local_y);
                            apply_3d_pick(s, mesh, face, pt);
                            picked_face = true;
                        }
                    });
                    if (!picked_face) {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "No face under cursor -- click on the mesh");
                    }
                    s.pick_mode = 0;
                    release_param_pick_input();
                } else if (clicked && !in_vp) {
                    // Click landed outside the 3D viewport -- keep pick mode on
                    // so the user can retry without re-pressing the button.
                }
            }

            // Draw a 3D point marker overlay (ImGui foreground) at the
            // projected screen position of the picked 3D point. Camera is
            // already sized to the FBO by ViewportCanvas, so we project
            // with the live camera state -- no temporary swap needed.
            // Camera::projectedCoordinatesOf already returns ImGui-style
            // (top-down) y, so we just translate by viewport_min; an extra
            // (vp_h - sp.y) flip would double-invert and throw the marker
            // off the viewport.
            if (s.last_result_valid && s.picked_3d_face >= 0 && viewer) {
                easy3d::vec3 p3d(s.picked_3d_x, s.picked_3d_y, s.picked_3d_z);
                easy3d::vec3 sp = viewer->camera()->projectedCoordinatesOf(p3d);
                if (sp.z >= 0.0f && sp.z <= 1.0f) {
                    const float sx = viewer->viewport_min_x() + sp.x;
                    const float sy = viewer->viewport_min_y() + sp.y;
                    // Stay inside the 3D panel: if the point is offscreen
                    // (camera rotated past it), do not draw at all.
                    if (sx >= viewer->viewport_min_x() &&
                        sx <= viewer->viewport_max_x() &&
                        sy >= viewer->viewport_min_y() &&
                        sy <= viewer->viewport_max_y())
                    {
                        auto* fg = ImGui::GetForegroundDrawList();
                        fg->AddCircleFilled(ImVec2(sx, sy), 7.0f,
                                            IM_COL32(20, 255, 60, 255));
                        fg->AddCircle(ImVec2(sx, sy), 10.0f,
                                      IM_COL32(0, 0, 0, 255), 0, 2.0f);
                        fg->AddCircle(ImVec2(sx, sy), 13.0f,
                                      IM_COL32(20, 255, 60, 200), 0, 1.5f);
                    }
                }
            }

            // Clean up highlight + overlay if the dialog is closed.
            if (!open && s.picked_3d_face >= 0) {
                clear_pick(s, mesh);
            }
#else
            ImGui::TextColored(claw_ui::status_warning_color(),
                "CGAL not available (rebuild with CLAW3D_ENABLE_CGAL=ON).");
#endif
        }
    } DIALOG_END;
}
