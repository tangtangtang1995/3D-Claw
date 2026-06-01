// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/arap_deformation_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "services/jobs/cgal/arap_deformation_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/graph.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/util/dialog.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include <fstream>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <GLFW/glfw3.h>

static const char* ARAP_HELP_PROMPT =
    "I am using CGAL As-Rigid-As-Possible (ARAP) Surface Mesh Deformation in 3D Claw.\n\n"
    "The workflow is:\n"
    "  1. Select a Region of Interest (ROI) on the mesh - the area that will deform.\n"
    "  2. Pick control vertices inside the ROI - these act as handles you can move.\n"
    "  3. Preprocess the ARAP system (solves the rigidity constraint).\n"
    "  4. Set target positions for control vertices by translation/rotation.\n"
    "  5. Deform the ROI - the algorithm minimizes stretching while keeping\n"
    "     the ROI boundary fixed.\n\n"
    "Three deformation modes:\n"
    "  SPOKES_AND_RIMS - default, usually the best balance of speed and quality.\n"
    "  ORIGINAL_ARAP  - classic ARAP, good for comparison.\n"
    "  SRE_ARAP       - smooth rotation enhanced, better for large rotations.\n\n"
    "Please suggest ROI size, control vertex placement, mode, and parameters\n"
    "for my mesh.";

namespace {

// --- k-ring extraction on Easy3D mesh ---

std::vector<int> extract_k_ring(easy3d::SurfaceMesh* mesh, int seed, int k) {
    std::vector<int> result;
    if (!mesh || seed < 0 || seed >= (int)mesh->n_vertices()) return result;
    if (mesh->is_isolated(easy3d::SurfaceMesh::Vertex(seed))) {
        result.push_back(seed);
        return result;
    }
    std::map<int,int> dist;
    std::queue<int> q;
    q.push(seed); dist[seed] = 0;
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int d = dist[cur];
        result.push_back(cur);
        if (d >= k) continue;
        auto v = easy3d::SurfaceMesh::Vertex(cur);
        for (auto h : mesh->halfedges(v)) {
            auto nv = mesh->target(h);
            int nid = (int)nv.idx();
            if (dist.find(nid) == dist.end()) {
                dist[nid] = d + 1;
                q.push(nid);
            }
        }
    }
    return result;
}

// --- Labels ---

const char* mode_label(int m) {
    switch (m) {
    case ARAP_MODE_SpokesAndRims: return "Spokes and Rims";
    case ARAP_MODE_OriginalARAP:  return "Original ARAP";
    case ARAP_MODE_SRE_ARAP:      return "SRE-ARAP";
    default:                       return "Unknown";
    }
}

const char* mode_hint(int m) {
    switch (m) {
    case ARAP_MODE_SpokesAndRims:
        return "Default. Best balance of speed and quality for most meshes.";
    case ARAP_MODE_OriginalARAP:
        return "Classic ARAP. Good for comparison with Spokes and Rims.";
    case ARAP_MODE_SRE_ARAP:
        return "Smooth Rotation Enhanced ARAP. Handles large rotations better "
               "but may be slower.";
    default: return "";
    }
}

// --- AI helpers ---

// ARAP keeps compact prompt formatting, but uses the shared claw_ai
// mesh collector so panel diagnostics stay consistent.
using claw_ai::ascii_only;

void append_mesh_metadata(std::ostringstream& oss, easy3d::SurfaceMesh* mesh) {
    const auto m = claw_ai::collect_surface_mesh_ai_stats(mesh);
    oss << "Source mesh:\n";
    oss << "- Name: " << m.name << " V=" << m.vertices << " F=" << m.faces << "\n";
    oss << "- Triangle mesh: " << (m.triangle_mesh ? "yes" : "no") << "\n";
    oss << "- Boundary edges: " << m.boundary_edges << "\n";
    if (m.bbox_valid) oss << "- BBox diagonal: " << m.bbox_diag << "\n";
}

std::string build_mesh_metadata_prompt(easy3d::SurfaceMesh* mesh) {
    std::ostringstream oss;
    append_mesh_metadata(oss, mesh);
    return ascii_only(oss.str());
}

// Parameter-advice prompt sent before a run.
std::string build_parameter_advice_prompt(easy3d::SurfaceMesh* mesh,
                                          const ARAPDeformationState& s)
{
    std::ostringstream out;
    out << ARAP_HELP_PROMPT << "\n\n";
    append_mesh_metadata(out, mesh);
    out << "\nCurrent selection:\n"
        << "  roi_vertices: " << s.roi_vertices.size() << "\n"
        << "  control_vertices: " << s.control_vertices.size() << "\n"
        << "  active_group: " << s.active_group << "\n"
        << "  roi_k_ring: " << s.roi_k_ring << "\n";
    out << "\nCurrent parameters:\n"
        << "  mode: " << mode_label(s.mode) << "\n"
        << "  iterations: " << s.iterations << "\n"
        << "  tolerance: " << s.tolerance << "\n";
    out << "\nCurrent transform for active group:\n"
        << "  translate_xyz: " << s.tx << " " << s.ty << " " << s.tz << "\n"
        << "  rotate_xyz_deg: " << s.rx_deg << " " << s.ry_deg << " " << s.rz_deg
        << "\n";
    out << "\nPlease answer in two parts:\n"
        << "1. A brief diagnosis (1 paragraph): is the input + selection\n"
        << "   suitable for ARAP, any preprocessing risks (too small ROI,\n"
        << "   too few controls, transform too large for the ROI).\n"
        << "2. Recommended parameters in this key=value block so the user\n"
        << "   can copy them back into the panel:\n"
        << "   mode=spokes|original|sre\n"
        << "   iterations=...\n"
        << "   tolerance=...\n"
        << "   roi_k_ring=...\n";
    return ascii_only(out.str());
}

// Result-evaluation prompt sent after a successful run.
std::string build_result_evaluation_prompt(const ARAPDeformationState& s,
                                            const ARAP_Result& r)
{
    std::ostringstream out;
    out << ARAP_HELP_PROMPT << "\n\n";
    out << "Run statistics:\n"
        << "  input_vertices: " << r.original_vertices << "\n"
        << "  input_faces:    " << r.original_faces << "\n"
        << "  roi_count:      " << r.roi_count << "\n"
        << "  control_count:  " << r.control_count << "\n"
        << "  iterations:     " << r.iterations << "\n"
        << "  elapsed_ms:     " << r.elapsed_ms << "\n"
        << "  max_displacement:  " << r.max_displacement << "\n"
        << "  mean_displacement: " << r.mean_displacement << "\n"
        << "  preprocess_ok:  " << (r.preprocess_ok ? "yes" : "no") << "\n";
    out << "\nParameters used:\n"
        << "  mode: " << mode_label(s.mode) << "\n"
        << "  iterations: " << s.iterations << "\n"
        << "  tolerance: " << s.tolerance << "\n"
        << "  translate_xyz: " << s.tx << " " << s.ty << " " << s.tz << "\n"
        << "  rotate_xyz_deg: " << s.rx_deg << " " << s.ry_deg << " " << s.rz_deg
        << "\n";
    if (r.error_code != ARAP_ERR_None)
        out << "\nerror: " << r.error_message << "\n";
    out << "\nPlease answer concisely (4-6 bullets):\n"
        << "1. Does the displacement magnitude look reasonable for the ROI size?\n"
        << "2. Likely artifacts (foldovers, stretching, rigid drift, "
              "boundary locking)?\n"
        << "3. Any sign the input or selection is unsuitable?\n"
        << "4. If rerunning, recommend ONE parameter or one selection change\n"
        << "   to try first.\n";
    return ascii_only(out.str());
}

// --- Selection save/load ---

bool save_arap_selection(const std::string& path,
                         easy3d::SurfaceMesh* mesh,
                         const ARAPDeformationState& s)
{
    std::ofstream out(path);
    if (!out) return false;
    out << "# ARAP Deformation Selection\n";
    out << "# version 1\n";
    out << "mesh " << (mesh ? mesh->name() : std::string("(none)")) << "\n";
    out << "mesh_vertices " << (mesh ? mesh->n_vertices() : 0) << "\n";
    out << "mesh_faces "    << (mesh ? mesh->n_faces()    : 0) << "\n";
    out << "roi";
    for (int v : s.roi_vertices) out << " " << v;
    out << "\n";
    out << "control";
    for (int v : s.control_vertices) out << " " << v;
    out << "\n";
    out << "groups";
    for (int g : s.control_group_ids) out << " " << g;
    out << "\n";
    out << "active_group " << s.active_group << "\n";
    out << "roi_k_ring "   << s.roi_k_ring << "\n";
    return (bool)out;
}

bool load_arap_selection(const std::string& path,
                         easy3d::SurfaceMesh* mesh,
                         ARAPDeformationState& s,
                         std::string& err)
{
    std::ifstream in(path);
    if (!in) { err = "cannot open file"; return false; }
    std::string line;
    int saved_mesh_vertices = -1;
    std::vector<int> new_roi, new_ctrl, new_groups;
    int new_active_group = s.active_group;
    int new_roi_k_ring   = s.roi_k_ring;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key; iss >> key;
        if (key == "mesh_vertices") {
            iss >> saved_mesh_vertices;
        } else if (key == "roi") {
            int v;
            while (iss >> v) new_roi.push_back(v);
        } else if (key == "control") {
            int v;
            while (iss >> v) new_ctrl.push_back(v);
        } else if (key == "groups") {
            int g;
            while (iss >> g) new_groups.push_back(g);
        } else if (key == "active_group") {
            iss >> new_active_group;
        } else if (key == "roi_k_ring") {
            iss >> new_roi_k_ring;
        }
        // mesh / mesh_faces ignored; we only check vertex count compatibility.
    }
    if (mesh && saved_mesh_vertices >= 0 &&
        (int)mesh->n_vertices() != saved_mesh_vertices)
    {
        err = "vertex count mismatch (file: " +
              std::to_string(saved_mesh_vertices) + ", current mesh: " +
              std::to_string(mesh->n_vertices()) + ")";
        return false;
    }
    // Filter out any out-of-range vertex ids defensively.
    if (mesh) {
        const int n = (int)mesh->n_vertices();
        auto in_range = [n](int v) { return v >= 0 && v < n; };
        new_roi.erase(std::remove_if(new_roi.begin(), new_roi.end(),
            [&](int v){ return !in_range(v); }), new_roi.end());
        // Keep control + groups parallel: drop the same indices in both.
        std::vector<int> filt_ctrl, filt_groups;
        for (size_t i = 0; i < new_ctrl.size(); ++i) {
            if (in_range(new_ctrl[i])) {
                filt_ctrl.push_back(new_ctrl[i]);
                filt_groups.push_back(i < new_groups.size() ? new_groups[i] : 0);
            }
        }
        new_ctrl   = std::move(filt_ctrl);
        new_groups = std::move(filt_groups);
    }
    s.roi_vertices      = std::move(new_roi);
    s.control_vertices  = std::move(new_ctrl);
    s.control_group_ids = std::move(new_groups);
    s.active_group      = new_active_group;
    s.roi_k_ring        = new_roi_k_ring;
    return true;
}

bool send_ai_prompt(MainWindow* win, const std::string& prompt,
                    const std::string& display_label = std::string()) {
    return claw_ai::send_panel_ai_prompt(win, prompt, display_label);
}

// --- Comma-separated index parsing ---

std::vector<int> parse_index_list(const char* text) {
    std::vector<int> out;
    if (!text || !text[0]) return out;
    std::string s(text);
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream iss(s);
    int v;
    while (iss >> v) out.push_back(v);
    return out;
}

std::string format_index_list(const std::vector<int>& v, int max_show = 20) {
    std::ostringstream oss;
    const int n = (int)v.size();
    const int limit = std::min(n, max_show);
    for (int i = 0; i < limit; ++i) {
        if (i > 0) oss << ", ";
        oss << v[i];
    }
    if (n > max_show) oss << ", ... (" << (n - max_show) << " more)";
    return oss.str();
}

// --- Target position computation for arrow overlays ---

// --- Quaternion helpers for drag-rotate (no Eigen needed here) ---

struct DragQuat { double w, x, y, z; };

DragQuat axis_angle_to_quat(double ax, double ay, double az, double angle_rad) {
    const double half = angle_rad * 0.5;
    const double s = std::sin(half);
    // Normalize axis defensively.
    const double len = std::sqrt(ax*ax + ay*ay + az*az);
    if (len < 1e-12) return {1.0, 0.0, 0.0, 0.0};
    ax /= len; ay /= len; az /= len;
    return {std::cos(half), ax * s, ay * s, az * s};
}

DragQuat quat_mul(const DragQuat& a, const DragQuat& b) {
    return {
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    };
}

// ZYX (Rz * Ry * Rx) Euler degrees -> quaternion.
DragQuat euler_zyx_deg_to_quat(double rx_deg, double ry_deg, double rz_deg) {
    const double d2r = 3.141592653589793 / 180.0;
    DragQuat qx = axis_angle_to_quat(1, 0, 0, rx_deg * d2r);
    DragQuat qy = axis_angle_to_quat(0, 1, 0, ry_deg * d2r);
    DragQuat qz = axis_angle_to_quat(0, 0, 1, rz_deg * d2r);
    return quat_mul(qz, quat_mul(qy, qx));
}

// Quaternion -> ZYX (Rz * Ry * Rx) Euler degrees, matching the runner's
// rotation order in apply_transform_to_origin.
void quat_to_euler_zyx_deg(const DragQuat& q, double& rx_deg,
                           double& ry_deg, double& rz_deg) {
    const double r2d = 180.0 / 3.141592653589793;
    // Standard ZYX extraction.
    const double sinp = 2.0 * (q.w * q.y - q.z * q.x);
    if (std::abs(sinp) >= 1.0) {
        // Gimbal lock: pitch == +-90 deg.
        ry_deg = (sinp > 0 ? 90.0 : -90.0);
        rx_deg = 0.0;
        rz_deg = std::atan2(-2.0 * (q.x * q.y - q.w * q.z),
                            1.0 - 2.0 * (q.y * q.y + q.z * q.z)) * r2d;
    } else {
        ry_deg = std::asin(sinp) * r2d;
        rx_deg = std::atan2(2.0 * (q.w * q.x + q.y * q.z),
                            1.0 - 2.0 * (q.x * q.x + q.y * q.y)) * r2d;
        rz_deg = std::atan2(2.0 * (q.w * q.z + q.x * q.y),
                            1.0 - 2.0 * (q.y * q.y + q.z * q.z)) * r2d;
    }
}

// Applies Euler ZYX rotation + translation to compute target positions.
// orig_ctrl_positions: parallel to the per-control list (NOT indexed by
// mesh vertex id). Each element is the current position of one control
// vertex; we enumerate them, NOT index by id, so this works regardless
// of which mesh vertex ids the controls happen to live at.
void compute_target_positions(
    const std::vector<ARAP_Point3d>& orig_ctrl_positions,
    double cx, double cy, double cz,  // rotation center
    double tx, double ty, double tz,
    double rx_deg, double ry_deg, double rz_deg,
    std::vector<easy3d::vec3>& from,
    std::vector<easy3d::vec3>& to)
{
    from.clear(); to.clear();
    from.reserve(orig_ctrl_positions.size());
    to.reserve(orig_ctrl_positions.size());
    const double deg2rad = 3.141592653589793 / 180.0;
    const double a = rx_deg * deg2rad;
    const double b = ry_deg * deg2rad;
    const double c = rz_deg * deg2rad;
    const double sa = std::sin(a), ca = std::cos(a);
    const double sb = std::sin(b), cb = std::cos(b);
    const double sc = std::sin(c), cc = std::cos(c);
    // R = Rz * Ry * Rx
    const double r00 = cc*cb, r01 = cc*sb*sa - sc*ca, r02 = cc*sb*ca + sc*sa;
    const double r10 = sc*cb, r11 = sc*sb*sa + cc*ca, r12 = sc*sb*ca - cc*sa;
    const double r20 = -sb,   r21 = cb*sa,            r22 = cb*ca;

    for (const auto& p : orig_ctrl_positions) {
        from.push_back(easy3d::vec3((float)p.x, (float)p.y, (float)p.z));
        const double dx = p.x - cx;
        const double dy = p.y - cy;
        const double dz = p.z - cz;
        const double rx = r00*dx + r01*dy + r02*dz;
        const double ry = r10*dx + r11*dy + r12*dz;
        const double rz = r20*dx + r21*dy + r22*dz;
        to.push_back(easy3d::vec3(
            (float)(rx + cx + tx),
            (float)(ry + cy + ty),
            (float)(rz + cz + tz)));
    }
}

// Rotation center for the active control group. We use the ROI centroid
// rather than the control centroid: a single control at the centroid of
// itself produces zero arc on rotation (the math is right but the UX has
// no visual feedback). ROI centroid makes single-control rotation visibly
// orbit, and feels closer to a CGAL Lab manipulated frame whose pivot is
// independent of the handles.
static void compute_rotation_center(easy3d::SurfaceMesh* mesh,
                                    const std::vector<int>& roi_vertices,
                                    const std::vector<int>& control_vertices,
                                    double& cx, double& cy, double& cz) {
    cx = cy = cz = 0.0;
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    int n = 0;
    const auto* src = &roi_vertices;
    if (src->empty()) src = &control_vertices; // fall back if no ROI yet
    for (int vid : *src) {
        if (vid < 0 || vid >= (int)mesh->n_vertices()) continue;
        auto v = easy3d::SurfaceMesh::Vertex(vid);
        if (!v.is_valid()) continue;
        const auto& p = pts[v];
        cx += p.x; cy += p.y; cz += p.z; ++n;
    }
    if (n > 0) { cx /= n; cy /= n; cz /= n; }
}

void push_arap_overlays(MainWindow* win,
                        easy3d::SurfaceMesh* mesh,
                        const ARAPDeformationState& s)
{
    if (!win || !mesh) return;
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");

    // ROI points (cyan).
    std::vector<easy3d::vec3> roi_pts;
    roi_pts.reserve(s.roi_vertices.size());
    for (int vid : s.roi_vertices) {
        auto v = easy3d::SurfaceMesh::Vertex(vid);
        if (v.is_valid() && vid < (int)mesh->n_vertices())
            roi_pts.push_back(pts[v]);
    }
    win->update_arap_roi_overlay(roi_pts);

    // Control points (color by group) + target arrows.
    std::vector<easy3d::vec3> ctrl_pts;
    std::vector<int> ctrl_groups;
    ctrl_pts.reserve(s.control_vertices.size());
    ctrl_groups.reserve(s.control_vertices.size());
    for (size_t i = 0; i < s.control_vertices.size(); ++i) {
        int vid = s.control_vertices[i];
        auto v = easy3d::SurfaceMesh::Vertex(vid);
        if (!v.is_valid() || vid >= (int)mesh->n_vertices()) continue;
        ctrl_pts.push_back(pts[v]);
        int gid = i < s.control_group_ids.size() ? s.control_group_ids[i] : 0;
        ctrl_groups.push_back(gid);
    }
    win->update_arap_ctrl_overlay(ctrl_pts, ctrl_groups);

    // Rotation center = ROI centroid (see comment on compute_rotation_center).
    double cx = 0, cy = 0, cz = 0;
    compute_rotation_center(mesh, s.roi_vertices, s.control_vertices,
                            cx, cy, cz);

    // Target arrows: from original ctrl positions -> transformed positions.
    std::vector<ARAP_Point3d> pod_orig;
    pod_orig.reserve(s.control_vertices.size());
    for (int vid : s.control_vertices) {
        if (vid < 0 || vid >= (int)mesh->n_vertices()) continue;
        auto v = easy3d::SurfaceMesh::Vertex(vid);
        if (!v.is_valid()) continue;
        const auto& p = pts[v];
        pod_orig.push_back({(double)p.x, (double)p.y, (double)p.z});
    }

    std::vector<easy3d::vec3> arrow_from, arrow_to;
    compute_target_positions(pod_orig,
                             cx, cy, cz, s.tx, s.ty, s.tz,
                             s.rx_deg, s.ry_deg, s.rz_deg,
                             arrow_from, arrow_to);
    win->update_arap_arrow_overlay(arrow_from, arrow_to);

    // Draw a 3-axis local frame indicator at the ROI centroid, rotated by
    // the current transform. Gives the user a visible coordinate gizmo
    // that tracks rotation even when there is only one control vertex.
    const auto& bb = mesh->bounding_box();
    const float axis_len = (float)((bb.is_valid()
        ? (double)bb.diagonal_length() : 1.0) * 0.10);
    win->update_arap_frame_overlay(
        easy3d::vec3((float)cx, (float)cy, (float)cz),
        s.tx, s.ty, s.tz,
        s.rx_deg, s.ry_deg, s.rz_deg,
        axis_len);
}

} // namespace

void renderDialogARAPDeformation(ViewportCanvas* viewer, ARAPDeformationState& s,
                                  bool& open) {
    // First-time size so Selection / Algorithm / Transform / Run sections
    // are all visible without scrolling. User can still resize freely.
    prepare_dialog_window(580.0f, 720.0f);
    DIALOG_BODY("ARAP Deformation", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win = MainWindow::instance();
        const bool busy = s.runner && win &&
            win->algorithm_controller().is_running_id(AlgorithmId::ArapDeformation);
        if (!open && busy) {
            open = true;
            s.close_requested = true;
            s.runner.cancel();
            glfwPostEmptyEvent();
        }
        if (!open && win) {
            win->clear_arap_overlay();
            win->clear_arap_preview_overlay(/*restore_source=*/true);
            // Always release camera lock so closing mid-drag doesn't
            // strand the viewer in unresponsive state.
            if (auto* vw = win->viewer()) vw->input_locked_ = false;
            s.owns_viewport_input_lock = false;
        }

        claw_ui::same_line_right_if_fits_button("?");
        if (ImGui::SmallButton("?")) {
            send_ai_prompt(win, ARAP_HELP_PROMPT, "Ask AI: ARAP deformation help");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ask AI to explain ARAP Deformation and "
                              "its parameters.");

        ImGui::Spacing();
        auto* viewer = win ? win->viewer() : nullptr;
        auto lock_arap_pick_input = [&]() {
            if (viewer) {
                viewer->input_locked_ = true;
                s.owns_viewport_input_lock = true;
            }
        };
        auto release_arap_pick_input = [&]() {
            if (viewer && s.owns_viewport_input_lock) {
                viewer->input_locked_ = false;
                s.owns_viewport_input_lock = false;
            }
        };

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
            s.drag_active = false;
            release_arap_pick_input();
            ImGui::TextColored(claw_ui::status_error_color(),
                "No surface mesh loaded.");
            if (ImGui::Button("Close")) open = false;
        } else {
            const int nv = (int)mesh->n_vertices();
            const int nf = (int)mesh->n_faces();
            const bool is_triangle = mesh->is_triangle_mesh();
            if (s.pick_mode != 0 && viewer && !busy)
                lock_arap_pick_input();
            else if (s.owns_viewport_input_lock)
                release_arap_pick_input();

            // --- Input ---
            ImGui::SeparatorText("Input");
            ImGui::TextColored(claw_ui::status_muted_color(),
                "%s (v=%d f=%d) triangle=%s",
                mesh->name().c_str(), nv, nf,
                is_triangle ? "yes" : "no");
            if (!is_triangle) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "ARAP requires a triangle mesh.");
            }

#ifdef CLAW3D_HAS_CGAL
            // Drag-handle handler. Three start paths:
            //   1) Click "Drag Active Group" -> pick_mode = 4 -> click in vp
            //   2) Shift + left-click in viewport -> translate-drag (no button needed)
            //   3) Ctrl  + left-click in viewport -> rotate-drag    (no button needed)
            // During an active drag the viewer's camera input is locked off
            // so the mouse exclusively operates on the active group transform.
            {
                auto& io = ImGui::GetIO();
                const bool in_vp = viewer && viewer->is_in_viewport(
                    io.MousePos.x, io.MousePos.y);
                const bool shift_quick_drag =
                    in_vp && !busy && !s.control_vertices.empty() &&
                    io.KeyShift && !io.KeyCtrl &&
                    ImGui::IsMouseClicked(0) && !s.drag_active;
                const bool ctrl_quick_drag =
                    in_vp && !busy && !s.control_vertices.empty() &&
                    io.KeyCtrl && !io.KeyShift &&
                    ImGui::IsMouseClicked(0) && !s.drag_active;
                const bool button_drag_ready =
                    s.pick_mode == 4 && in_vp && !busy &&
                    ImGui::IsMouseClicked(0) && !s.drag_active;
                if (!s.drag_active && (shift_quick_drag || ctrl_quick_drag ||
                                        button_drag_ready)) {
                    s.drag_active   = true;
                    // Button-mode defaults to translate unless Ctrl is held.
                    s.drag_mode     = (ctrl_quick_drag ||
                                       (button_drag_ready && io.KeyCtrl)) ? 1 : 0;
                    s.drag_start_mx = io.MousePos.x;
                    s.drag_start_my = io.MousePos.y;
                    s.drag_start_tx = s.tx;
                    s.drag_start_ty = s.ty;
                    s.drag_start_tz = s.tz;
                    s.drag_start_rx_deg = s.rx_deg;
                    s.drag_start_ry_deg = s.ry_deg;
                    s.drag_start_rz_deg = s.rz_deg;
                    auto* cam = viewer->camera();
                    const easy3d::vec3 r = cam->rightVector();
                    const easy3d::vec3 u = cam->upVector();
                    s.drag_cam_rx = (double)r.x;
                    s.drag_cam_ry = (double)r.y;
                    s.drag_cam_rz = (double)r.z;
                    s.drag_cam_ux = (double)u.x;
                    s.drag_cam_uy = (double)u.y;
                    s.drag_cam_uz = (double)u.z;
                    const auto& bb = mesh->bounding_box();
                    const double diag =
                        (bb.is_valid() ? (double)bb.diagonal_length() : 1.0);
                    const int vp_h = std::max(1, (int)(
                        viewer->viewport_max_y() - viewer->viewport_min_y()));
                    s.drag_sensitivity = diag / (double)vp_h;
                    // Rotation: a full viewport-height drag = 180 degrees.
                    s.drag_rot_sensitivity_rad_per_px =
                        3.141592653589793 / (double)vp_h;
                    viewer->input_locked_ = true;
                    std::snprintf(s.pick_status, sizeof(s.pick_status),
                        "Drag started (group %d, %s, camera locked)",
                        s.active_group,
                        s.drag_mode == 1 ? "rotate" : "translate");
                } else if (s.drag_active) {
                    // Drag in progress.
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    const float dx_px = io.MousePos.x - s.drag_start_mx;
                    const float dy_px = -(io.MousePos.y - s.drag_start_my);
                    if (s.drag_mode == 0) {
                        // Translate.
                        const double wdx = (double)dx_px * s.drag_sensitivity;
                        const double wdy = (double)dy_px * s.drag_sensitivity;
                        s.tx = s.drag_start_tx + wdx * s.drag_cam_rx
                                               + wdy * s.drag_cam_ux;
                        s.ty = s.drag_start_ty + wdx * s.drag_cam_ry
                                               + wdy * s.drag_cam_uy;
                        s.tz = s.drag_start_tz + wdx * s.drag_cam_rz
                                               + wdy * s.drag_cam_uz;
                    } else {
                        // Rotate (view-aligned trackball-lite):
                        //   horizontal drag -> yaw around camera-up
                        //   vertical drag   -> pitch around camera-right
                        // Compose with the rotation captured at drag-start so
                        // numeric Rotate XYZ stays in sync with the drag.
                        const double yaw_rad =
                            (double)dx_px * s.drag_rot_sensitivity_rad_per_px;
                        const double pitch_rad =
                            (double)dy_px * s.drag_rot_sensitivity_rad_per_px;
                        DragQuat q_yaw = axis_angle_to_quat(
                            s.drag_cam_ux, s.drag_cam_uy, s.drag_cam_uz, yaw_rad);
                        DragQuat q_pitch = axis_angle_to_quat(
                            s.drag_cam_rx, s.drag_cam_ry, s.drag_cam_rz, pitch_rad);
                        DragQuat q_base = euler_zyx_deg_to_quat(
                            s.drag_start_rx_deg,
                            s.drag_start_ry_deg,
                            s.drag_start_rz_deg);
                        DragQuat q_new = quat_mul(q_yaw,
                                          quat_mul(q_pitch, q_base));
                        quat_to_euler_zyx_deg(q_new,
                            s.rx_deg, s.ry_deg, s.rz_deg);
                    }
                    push_arap_overlays(win, mesh, s);
                    if (ImGui::IsMouseReleased(0)) {
                        s.drag_active = false;
                        s.pick_mode = 0;
                        if (viewer) viewer->input_locked_ = false;
                        s.owns_viewport_input_lock = false;
                        s.auto_run_pending = true;
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "Drag released (%s) -> running deform...",
                            s.drag_mode == 1 ? "rotate" : "translate");
                        glfwPostEmptyEvent();
                    }
                }
            }

            // Vertex picking handler (one-shot: pick face + adsorb to nearest
            // vertex). Runs before the selection UI so pick result is applied
            // within the same frame.
            if (s.pick_mode != 0 && s.pick_mode != 4 &&
                viewer && !busy) {
                auto& io = ImGui::GetIO();
                const bool clicked = ImGui::IsMouseClicked(0);
                const bool in_vp = viewer->is_in_viewport(io.MousePos.x, io.MousePos.y);
                if (clicked && in_vp) {
                    const float local_x = io.MousePos.x - viewer->viewport_min_x();
                    const float local_y = io.MousePos.y - viewer->viewport_min_y();
                    auto* cam = viewer->camera();
                    const int prev_sw = cam->screenWidth();
                    const int prev_sh = cam->screenHeight();
                    const int vp_w = (int)(viewer->viewport_max_x() -
                                           viewer->viewport_min_x());
                    const int vp_h = (int)(viewer->viewport_max_y() -
                                           viewer->viewport_min_y());
                    cam->setScreenWidthAndHeight(vp_w, vp_h);
                    easy3d::SurfaceMeshPicker picker(cam);
                    auto face = picker.pick_face(mesh, (int)local_x, (int)local_y);
                    int best_vid = -1;
                    if (face.is_valid()) {
                        auto pt = picker.picked_point(mesh, face,
                                                      (int)local_x, (int)local_y);
                        float best_d2 = FLT_MAX;
                        for (auto v : mesh->vertices(face)) {
                            auto pos = mesh->position(v);
                            float dx = pos.x - pt.x;
                            float dy = pos.y - pt.y;
                            float dz = pos.z - pt.z;
                            float d2 = dx*dx + dy*dy + dz*dz;
                            if (d2 < best_d2) {
                                best_d2 = d2;
                                best_vid = (int)v.idx();
                            }
                        }
                    }
                    cam->setScreenWidthAndHeight(prev_sw, prev_sh);
                    if (face.is_valid() && best_vid >= 0) {
                        if (s.pick_mode == 1) { // ROI Seed
                            s.roi_seed = best_vid;
                            std::snprintf(s.pick_status, sizeof(s.pick_status),
                                "ROI seed picked: %d", best_vid);
                        } else if (s.pick_mode == 2) { // Control
                            bool dup = false;
                            for (int vv : s.control_vertices)
                                if (vv == best_vid) { dup = true; break; }
                            if (!dup) {
                                s.control_vertices.push_back(best_vid);
                                s.control_group_ids.push_back(s.active_group);
                                std::snprintf(s.pick_status, sizeof(s.pick_status),
                                    "Control %d added (group %d)", best_vid,
                                    s.active_group);
                            } else {
                                std::snprintf(s.pick_status, sizeof(s.pick_status),
                                    "Vertex %d already a control", best_vid);
                            }
                        } else if (s.pick_mode == 3) { // Erase
                            // Remove from ROI
                            auto rit = std::find(s.roi_vertices.begin(),
                                                 s.roi_vertices.end(), best_vid);
                            if (rit != s.roi_vertices.end())
                                s.roi_vertices.erase(rit);
                            // Remove from controls
                            for (size_t ci = 0; ci < s.control_vertices.size(); ) {
                                if (s.control_vertices[ci] == best_vid) {
                                    s.control_vertices.erase(
                                        s.control_vertices.begin() + ci);
                                    s.control_group_ids.erase(
                                        s.control_group_ids.begin() + ci);
                                } else ++ci;
                            }
                            std::snprintf(s.pick_status, sizeof(s.pick_status),
                                "Erased vertex %d", best_vid);
                        }
                        push_arap_overlays(win, mesh, s);
                    } else {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "No vertex picked (click on the surface)");
                    }
                    s.pick_mode = 0;
                    release_arap_pick_input();
                } else if (clicked && !in_vp) {
                    s.pick_mode = 0;
                    release_arap_pick_input();
                    std::snprintf(s.pick_status, sizeof(s.pick_status),
                        "Clicked outside viewport");
                }
            }

            // --- Selection ---
            ImGui::SeparatorText("Selection");
            // Pick buttons
            {
                const bool picking = s.pick_mode != 0;
                ImGui::BeginDisabled(picking);
                if (ImGui::SmallButton("Pick ROI Seed")) {
                    s.pick_mode = 1;
                    s.pick_status[0] = '\0';
                    lock_arap_pick_input();
                }
                claw_ui::same_line_if_fits_button("Pick Control");
                if (ImGui::SmallButton("Pick Control")) {
                    s.pick_mode = 2;
                    s.pick_status[0] = '\0';
                    lock_arap_pick_input();
                }
                claw_ui::same_line_if_fits_button("Erase Picked");
                if (ImGui::SmallButton("Erase Picked")) {
                    s.pick_mode = 3;
                    s.pick_status[0] = '\0';
                    lock_arap_pick_input();
                }
                ImGui::EndDisabled();
                if (picking) {
                    claw_ui::same_line_if_fits_width(260.0f);
                    if (s.pick_mode == 1) ImGui::TextColored(
                        claw_ui::status_success_color(), "Click mesh to pick ROI seed...");
                    else if (s.pick_mode == 2) ImGui::TextColored(
                        claw_ui::status_success_color(), "Click mesh to add control...");
                    else if (s.pick_mode == 3) ImGui::TextColored(
                        claw_ui::status_warning_color(), "Click mesh to erase vertex...");
                    else if (s.pick_mode == 4 && !s.drag_active) ImGui::TextColored(
                        claw_ui::status_success_color(), "Press and drag in viewport to move active group...");
                    else if (s.pick_mode == 4 && s.drag_active) ImGui::TextColored(
                        claw_ui::status_warning_color(), "Dragging (release to finish)...");
                }
                if (s.pick_status[0]) {
                    claw_ui::same_line_if_fits_width(200.0f);
                    ImGui::TextDisabled("%s", s.pick_status);
                }
            }

            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("ROI Seed", &s.roi_seed, 1, 1);
            if (s.roi_seed < 0) s.roi_seed = 0;
            if (s.roi_seed >= nv) s.roi_seed = nv - 1;
            claw_ui::same_line_if_fits_width(220.0f);
            ImGui::SetNextItemWidth(100);
            ImGui::SliderInt("K-Ring", &s.roi_k_ring, 1, 30);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "ROI = graph k-ring around the seed vertex.\n"
                    "Larger K = bigger editable region, smoother deformation,\n"
                    "  but slower preprocess and weaker control near boundary.\n"
                    "Smaller K = stiffer deformation, sharp transition at boundary.\n"
                    "Recommended: 6-12 for organic shapes, smaller for CAD-like.");
            if (ImGui::Button("Grow ROI")) {
                if (mesh && s.roi_seed >= 0 && s.roi_seed < nv) {
                    s.roi_vertices = extract_k_ring(mesh, s.roi_seed, s.roi_k_ring);
                    push_arap_overlays(win, mesh, s);
                }
            }
            claw_ui::same_line_if_fits_button("Clear ROI");
            if (ImGui::Button("Clear ROI")) { s.roi_vertices.clear(); push_arap_overlays(win, mesh, s); }
            ImGui::TextDisabled("ROI: %d vertices  |  %s",
                (int)s.roi_vertices.size(),
                s.roi_vertices.empty() ? "(empty)"
                    : format_index_list(s.roi_vertices).c_str());

            ImGui::Spacing();
            // Control vertex entry
            {
                static int ctrl_vid = 0;
                ImGui::SetNextItemWidth(120);
                ImGui::InputInt("Control Vertex", &ctrl_vid, 1, 1);
                if (ctrl_vid < 0) ctrl_vid = 0;
                if (ctrl_vid >= nv) ctrl_vid = nv - 1;
                claw_ui::same_line_if_fits_width(160.0f);
                ImGui::SetNextItemWidth(80);
                ImGui::InputInt("Group", &s.active_group, 1, 1);
                if (s.active_group < 0) s.active_group = 0;
                claw_ui::same_line_if_fits_button("Add Control");
                if (ImGui::Button("Add Control")) {
                    s.control_vertices.push_back(ctrl_vid);
                    s.control_group_ids.push_back(s.active_group);
                    push_arap_overlays(win, mesh, s);
                }
            }
            claw_ui::same_line_if_fits_button("Clear Controls");
            if (ImGui::Button("Clear Controls")) {
                s.control_vertices.clear();
                s.control_group_ids.clear();
                push_arap_overlays(win, mesh, s);
            }
            ImGui::TextDisabled("Controls: %d vertices in %d groups",
                (int)s.control_vertices.size(),
                s.control_group_ids.empty() ? 0
                    : 1 + (int)*std::max_element(s.control_group_ids.begin(),
                                                  s.control_group_ids.end()));

            if (!s.control_vertices.empty() && !s.control_group_ids.empty()) {
                ImGui::TextDisabled("  ctrl[%zu] = %s",
                    s.control_vertices.size(),
                    format_index_list(s.control_vertices).c_str());
            }

            // Selection save / load + AI parameter advice.
            ImGui::Spacing();
            if (ImGui::SmallButton("Save Selection...")) {
                std::string default_name = mesh ? mesh->name() : "selection";
                if (easy3d::file_system::extension(default_name).empty())
                    default_name += ".arapsel";
                else
                    default_name = easy3d::file_system::base_name(default_name)
                                   + ".arapsel";
                auto path = easy3d::dialog::save(
                    "Save ARAP Selection", default_name,
                    {"ARAP Selection (*.arapsel)", "*.arapsel",
                     "All Files (*.*)", "*"});
                if (!path.empty()) {
                    if (save_arap_selection(path, mesh, s)) {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "Saved selection to %s",
                            easy3d::file_system::simple_name(path).c_str());
                    } else {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "Save failed");
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Save the current ROI + control selection to a .arapsel\n"
                    "text file. Re-loadable later on the SAME mesh (vertex\n"
                    "count must match) to skip picking again.");
            claw_ui::same_line_if_fits_button("Load Selection...");
            if (ImGui::SmallButton("Load Selection...")) {
                auto paths = easy3d::dialog::open(
                    "Load ARAP Selection", "",
                    {"ARAP Selection (*.arapsel)", "*.arapsel",
                     "All Files (*.*)", "*"}, false);
                if (!paths.empty()) {
                    std::string err;
                    if (load_arap_selection(paths.front(), mesh, s, err)) {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "Loaded selection (%zu roi, %zu ctrl)",
                            s.roi_vertices.size(), s.control_vertices.size());
                        push_arap_overlays(win, mesh, s);
                    } else {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "Load failed: %s", err.c_str());
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Load a previously saved .arapsel into the current mesh.\n"
                    "Vertex count must match - otherwise vertex ids cannot\n"
                    "be trusted and the load is rejected.");
            claw_ui::same_line_if_fits_button("AI Parameter Advice##pre");
            if (mesh && ImGui::SmallButton("AI Parameter Advice##pre")) {
                send_ai_prompt(win,
                    build_parameter_advice_prompt(mesh, s),
                    "Ask AI: ARAP parameter advice");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Ask AI for ARAP mode + iter + tolerance + ROI sizing\n"
                    "recommendations based on the current mesh and selection.\n"
                    "AI replies in a key=value block you can copy back here.");
            if (mesh && win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                claw_ui::same_line_if_fits_text("(set API key in AI Chat)");
                ImGui::TextDisabled("(set API key in AI Chat)");
            }

            // Early visibility hint: if user just picked control(s) and
            // hasn't set a transform yet, target arrows are zero-length
            // and invisible. Tell them right here, not 100px below.
            {
                const bool zero_xf = (s.tx == 0.0 && s.ty == 0.0 && s.tz == 0.0 &&
                                      s.rx_deg == 0.0 && s.ry_deg == 0.0 &&
                                      s.rz_deg == 0.0);
                if (!s.control_vertices.empty() && zero_xf) {
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "[i] Yellow target arrows appear once you set a Translate or Rotate below.");
                }
            }

            // --- Algorithm ---
            ImGui::SeparatorText("Algorithm");
            {
                const char* items[] = {
                    "Spokes and Rims",
                    "Original ARAP",
                    "SRE-ARAP"
                };
                ImGui::Combo("Mode", &s.mode, items, IM_ARRAYSIZE(items));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Deformation algorithm tag.\n"
                        "Spokes and Rims (default): best speed/quality balance.\n"
                        "Original ARAP: classic, good baseline for comparison.\n"
                        "SRE-ARAP: smoother under large rotations, slower.\n"
                        "When in doubt, keep Spokes and Rims.");
            }
            ImGui::TextWrapped("%s", mode_hint(s.mode));
            ImGui::Spacing();
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Iterations", &s.iterations, 1, 5);
            if (s.iterations < 1) s.iterations = 1;
            if (s.iterations > 200) s.iterations = 200;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Inner ARAP iterations per deform() call.\n"
                    "More iterations -> closer to equilibrium, slower.\n"
                    "10 is fine for previews; 30-50 for final result.\n"
                    "If the result looks 'unsettled', raise this first.");
            ImGui::SetNextItemWidth(200);
            ImGui::InputDouble("Tolerance", &s.tolerance, 1e-5, 1e-3, "%.6f");
            if (s.tolerance < 1e-10) s.tolerance = 1e-10;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Convergence threshold passed to CGAL deform(iter, tol).\n"
                    "Iteration stops early once the residual drops below this.\n"
                    "1e-4 default. Smaller = tighter, more iterations used.");

            // --- Transform ---
            ImGui::SeparatorText("Transform");
            ImGui::TextDisabled("Active group: %d", s.active_group);
            float txf[3] = {(float)s.tx, (float)s.ty, (float)s.tz};
            ImGui::InputFloat3("Translate (XYZ)", txf, "%.3f");
            s.tx = txf[0]; s.ty = txf[1]; s.tz = txf[2];
            float rxf[3] = {(float)s.rx_deg, (float)s.ry_deg, (float)s.rz_deg};
            ImGui::InputFloat3("Rotate (XYZ deg)", rxf, "%.2f");
            s.rx_deg = rxf[0]; s.ry_deg = rxf[1]; s.rz_deg = rxf[2];

            // One-click sample so the user can immediately see yellow
            // arrows + target marker, without typing values.
            if (ImGui::Button("Set Sample Translate")) {
                double bbox_diag = 1.0;
                if (mesh) {
                    const auto& bb = mesh->bounding_box();
                    if (bb.is_valid())
                        bbox_diag = (double)bb.diagonal_length();
                }
                if (bbox_diag <= 0.0) bbox_diag = 1.0;
                s.tx = 0.0;
                s.ty = 0.1 * bbox_diag;
                s.tz = 0.0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Set Translate Y to 0.1 * bbox diagonal so target\n"
                    "arrows become visible. You can edit the values\n"
                    "afterwards or click Reset Transform.");
            claw_ui::same_line_if_fits_button("Reset Transform");
            if (ImGui::Button("Reset Transform")) {
                s.tx = s.ty = s.tz = 0.0;
                s.rx_deg = s.ry_deg = s.rz_deg = 0.0;
            }
            claw_ui::same_line_if_fits_button("Drag Active Group");
            // Drag handle. Disabled while a run is in flight or while
            // there are no control vertices to move.
            const bool can_drag =
                !busy && !s.control_vertices.empty();
            ImGui::BeginDisabled(!can_drag);
            if (ImGui::Button("Drag Active Group")) {
                s.pick_mode = 4;
                s.drag_active = false;
                s.pick_status[0] = '\0';
                lock_arap_pick_input();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Drag the active control group in the viewport.\n"
                    "Updates numeric Translate/Rotate fields + yellow\n"
                    "target arrows in real time. The mesh deforms\n"
                    "automatically when you release the mouse.\n\n"
                    "Shortcuts (no need to click this button first):\n"
                    "  Shift + left-drag  -> translate (along view plane)\n"
                    "  Ctrl  + left-drag  -> rotate (yaw/pitch trackball)\n\n"
                    "Camera input is locked during the drag so the view\n"
                    "stays still while you manipulate the group.");
            }
            if (s.pick_mode == 4) {
                claw_ui::same_line_if_fits_button("Cancel Drag");
                if (ImGui::Button("Cancel Drag")) {
                    s.pick_mode = 0;
                    s.drag_active = false;
                    if (viewer) viewer->input_locked_ = false;
                    s.owns_viewport_input_lock = false;
                }
            }

            // Reset Preview: clear the live overlay so the user can compare
            // current selection without a leftover deformed mesh on screen.
            if (win && win->has_arap_preview_overlay()) {
                claw_ui::same_line_if_fits_button("Reset Preview");
                if (ImGui::Button("Reset Preview")) {
                    win->clear_arap_preview_overlay(/*restore_source=*/true);
                }
            }

            // Always-visible transform status row so the user can see at
            // a glance what target arrows currently encode.
            {
                const bool zero_xf = (s.tx == 0.0 && s.ty == 0.0 && s.tz == 0.0 &&
                                      s.rx_deg == 0.0 && s.ry_deg == 0.0 &&
                                      s.rz_deg == 0.0);
                if (zero_xf) {
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Transform = identity -> no target arrows yet. "
                        "Edit translate/rotate or click Set Sample Translate.");
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Transform set -> yellow arrows = control -> target.");
                }
            }

            // Live preview controls.
            ImGui::Spacing();
            ImGui::Checkbox("Live Preview", &s.live_preview);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Interpolate the transform from identity to target over\n"
                    "N steps, calling CGAL deform() each step. The mesh\n"
                    "smoothly progresses toward its final pose so you can\n"
                    "see how the ROI absorbs the handle's motion. Final\n"
                    "result matches Fast mode within tolerance.");
            if (s.live_preview) {
                claw_ui::same_line_if_fits_width(180.0f);
                const char* speeds[] = {"Fast", "Normal", "Slow"};
                ImGui::SetNextItemWidth(120.0f);
                ImGui::Combo("##arap_speed",
                             &s.preview_speed, speeds, IM_ARRAYSIZE(speeds));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "UI pacing only - does NOT change the numerical result.\n"
                        "Fast: ~60 ms/step.\n"
                        "Normal: ~140 ms/step.\n"
                        "Slow: ~320 ms/step (good for teaching/inspection).");
                claw_ui::same_line_if_fits_width(150.0f);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::InputInt("Steps", &s.preview_steps, 1, 4);
                if (s.preview_steps < 2)   s.preview_steps = 2;
                if (s.preview_steps > 128) s.preview_steps = 128;
            }

            // Per-frame overlay refresh so arrows track transform changes.
            // Only when no run is in flight; live preview's worker owns the
            // preview mesh + ROI/control overlays are static during a run.
            if (!busy) push_arap_overlays(win, mesh, s);

            // --- Run / Cancel ---
            ImGui::Spacing();
            if (!busy) {
                const bool can_run = is_triangle &&
                    !s.roi_vertices.empty() && !s.control_vertices.empty();
                ImGui::BeginDisabled(!can_run);
                const bool clicked_run = ImGui::Button("Run");
                ImGui::EndDisabled();
                // Auto-run path: drag-release sets auto_run_pending=true,
                // and we trigger the same Run code as if the button was
                // clicked (subject to the same validity check).
                const bool trigger_run =
                    clicked_run || (s.auto_run_pending && can_run);
                s.auto_run_pending = false;
                if (trigger_run) {
                    ARAP_Selection sel;
                    sel.roi_vertices      = s.roi_vertices;
                    sel.control_vertices  = s.control_vertices;
                    sel.control_group_ids = s.control_group_ids;

                    ARAP_ControlTransform xf;
                    xf.group_id = s.active_group;
                    xf.tx = s.tx; xf.ty = s.ty; xf.tz = s.tz;
                    xf.rx_deg = s.rx_deg; xf.ry_deg = s.ry_deg;
                    xf.rz_deg = s.rz_deg;

                    ARAP_Config cfg;
                    cfg.mode          = s.mode;
                    cfg.iterations    = s.iterations;
                    cfg.tolerance     = s.tolerance;
                    cfg.live_preview  = s.live_preview;
                    cfg.preview_speed = s.preview_speed;
                    cfg.preview_steps = s.preview_steps;

                    s.last_result_valid = false;
                    s.last_input_metadata_prompt =
                        build_mesh_metadata_prompt(mesh);
                    s.close_requested = false;
                    s.last_error.clear();
                    s.final_result_ready.store(false,
                        std::memory_order_release);
                    s.last_snap_gen   = -1;
                    s.settling        = false;
                    s.settle_started_at = 0.0;

                    if (s.live_preview)
                        win->init_arap_preview_overlay(mesh);

                    claw3d::services::ArapDeformationJobStart request;
                    request.source_mesh = mesh;
                    request.source_handle =
                        (win && win->viewer())
                            ? win->viewer()->model_handle(mesh)
                            : ModelHandle{};
                    request.config = cfg;
                    request.selection = sel;
                    request.control_transform = xf;
                    request.source_name = mesh->name();
                    request.final_result_ready = &s.final_result_ready;
                    request.wake_ui = []() { glfwPostEmptyEvent(); };

                    if (win) {
                        s.runner = claw3d::services::start_arap_deformation_job(
                            win->algorithm_controller(), request);
                    } else {
                        s.runner.reset();
                    }
                    if (!s.runner) {
                        if (s.live_preview && win)
                            win->clear_arap_preview_overlay(
                                /*restore_source=*/true);
                        s.final_result_ready.store(true,
                            std::memory_order_release);
                        s.last_error = "Failed to start ARAP deformation job.";
                        LOG(WARNING) << s.last_error;
                    }
                }
                if (s.roi_vertices.empty())
                    ImGui::TextDisabled("Pick or grow an ROI first.");
                if (s.control_vertices.empty())
                    ImGui::TextDisabled("Add at least one control vertex.");
            } else {
                // Drain the latest live-preview snapshot so the overlay
                // mesh keeps up with the worker.
                if (s.live_preview && s.runner && win) {
                    ARAP_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                        !snap.vertices.empty())
                    {
                        win->update_arap_preview_overlay(snap);
                    }
                }
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Running ARAP deformation...");
                if (s.live_preview && s.runner) {
                    const auto& latest = s.last_snap_gen;
                    claw_ui::same_line_if_fits_text("(snapshot #000000)");
                    ImGui::TextDisabled("(snapshot #%d)", latest);
                }
                if (ImGui::Button("Cancel")) {
                    s.runner.cancel();
                    glfwPostEmptyEvent();
                }
                if (s.runner && s.runner.is_cancelled()) {
                    claw_ui::same_line_if_fits_text("Cancel requested...");
                    ImGui::TextDisabled("Cancel requested...");
                }
                if (s.close_requested) {
                    claw_ui::same_line_if_fits_text("Closing after run stops...");
                    ImGui::TextDisabled("Closing after run stops...");
                }
            }

            // Worker completion -> optional settle, then finalize.
            if (s.runner && busy && s.runner.is_done() &&
                s.final_result_ready.load(std::memory_order_acquire))
            {
                const bool close_after = s.close_requested;
                const bool finish_immediately =
                    !s.live_preview || close_after ||
                    s.runner.is_cancelled() || s.runner.has_error();
                if (finish_immediately) {
                    if (s.live_preview && win)
                        win->clear_arap_preview_overlay(/*restore_source=*/true);
                    if (s.runner.has_error())
                        s.last_error = s.runner.last_error();
                    s.runner.get_result(s.last_result);
                    s.last_result_valid = true;
                    mark_algorithm_done(win);
                    s.runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    if (close_after) open = false;
                } else if (!s.settling) {
                    // Drain one last snapshot so overlay matches the final
                    // deformed pose during the hold.
                    ARAP_Snapshot snap;
                    if (s.runner.poll_snapshot(s.last_snap_gen, snap) &&
                        win && !snap.vertices.empty())
                    {
                        win->update_arap_preview_overlay(snap);
                    }
                    if (s.runner.has_error())
                        s.last_error = s.runner.last_error();
                    s.runner.get_result(s.last_result);
                    s.last_result_valid = true;
                    s.settling = true;
                    s.settle_started_at = ImGui::GetTime();
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding deformed mesh for %.1fs ...",
                        s.settle_ms / 1000.0);
                } else {
                    const double now = ImGui::GetTime();
                    const double elapsed =
                        (now - s.settle_started_at) * 1000.0;
                    if (elapsed >= s.settle_ms) {
                        // Normal end. Hide the source mesh entirely so the
                        // user only sees the final deformed result, and
                        // clear all ARAP selection / arrow / frame overlays
                        // since they describe the SOURCE selection and would
                        // look stale against the deformed mesh.
                        if (win) {
                            win->clear_arap_preview_overlay(/*restore_source=*/true);
                            win->clear_arap_overlay();
                            if (mesh && mesh->renderer())
                                mesh->renderer()->set_visible(false);
                            win->viewer()->mark_dirty();
                        }
                        mark_algorithm_done(win);
                        s.runner.reset();
                        s.settling = false;
                        s.close_requested = false;
                        glfwPostEmptyEvent();
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Done. Holding deformed mesh %.1fs / %.1fs ...",
                            elapsed / 1000.0, s.settle_ms / 1000.0);
                    }
                }
            }

            // Post-run display.
            if (s.last_result_valid && !busy) {
                const auto& r = s.last_result;
                ImGui::Spacing();
                if (r.error_code != ARAP_ERR_None) {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", r.error_message);
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Result: ROI=%d controls=%d | "
                        "%.0f ms | max_disp=%.5f mean_disp=%.5f",
                        r.roi_count, r.control_count,
                        r.elapsed_ms, r.max_displacement,
                        r.mean_displacement);
                    ImGui::TextDisabled(
                        "Mode: %s | iters=%d | preprocess=%s",
                        mode_label(s.mode), r.iterations,
                        r.preprocess_ok ? "OK" : "FAIL");
                }
                if (!s.last_error.empty())
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", s.last_error.c_str());
                if (ImGui::Button("AI Evaluate Result")) {
                    send_ai_prompt(win,
                        build_result_evaluation_prompt(s, r),
                        "Ask AI: evaluate ARAP result");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Ask AI to evaluate the deformation result\n"
                        "(displacement magnitude, likely artifacts,\n"
                        "selection adequacy) and recommend the next\n"
                        "parameter or selection change to try.");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("(set API key in AI Chat)");
                    ImGui::TextDisabled("(set API key in AI Chat)");
                }
                claw_ui::same_line_if_fits_button("AI Parameter Advice");
                if (ImGui::Button("AI Parameter Advice")) {
                    send_ai_prompt(win,
                        build_parameter_advice_prompt(mesh, s),
                        "Ask AI: ARAP parameter advice");
                }
            }
#else
            ImGui::TextColored(claw_ui::status_warning_color(),
                "CGAL not available (rebuild with CLAW3D_ENABLE_CGAL=ON).");
#endif
        }
    } DIALOG_END;
}
