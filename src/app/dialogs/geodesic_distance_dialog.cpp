// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/geodesic_distance_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/preview_policy.h"
#include "ai/ai_prompt_utils.h"
#include "ai/mesh_ai_stats.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/gui/picker_surface_mesh.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>

// ============================================================================
// AI helpers
// ============================================================================

namespace {

const char* mode_label(int m) {
    switch (m) {
    case GEO_MODE_FrontPropagation:  return "Front Propagation (Easy3D)";
    case GEO_MODE_ExactShortestPath: return "Exact Shortest Path (CGAL)";
    case GEO_MODE_HeatMethod:        return "Heat Method (CGAL)";
    default:                          return "Unknown";
    }
}

const char* mode_hint(int m) {
    switch (m) {
    case GEO_MODE_FrontPropagation:
        return "Best for explaining geodesic propagation. The animation shows "
               "the actual front expanding over the mesh. Use this mode for "
               "teaching, visual debugging, and seed-region exploration.";
    case GEO_MODE_ExactShortestPath:
        return "Best for accurate source-to-target paths. It computes exact "
               "shortest paths, but it is not a wavefront animation algorithm.";
    case GEO_MODE_HeatMethod:
        return "Best for fast distance fields over the whole mesh. It gives "
               "an all-vertex heatmap and is ideal for multi-source distance "
               "analysis.";
    default:
        return "";
    }
}

bool is_live_vertex(easy3d::SurfaceMesh* mesh, int vid) {
    if (!mesh || vid < 0 || vid >= (int)mesh->vertices_size())
        return false;
    easy3d::SurfaceMesh::Vertex v(vid);
    return mesh->is_valid(v) &&
        !(mesh->has_garbage() && mesh->is_deleted(v));
}

// Geodesic keeps a compact, geodesic-specific metadata formatting, but
// uses the shared claw_ai mesh collector so mesh diagnostics stay
// consistent across algorithm panels.
using claw_ai::ascii_only;

const char* GEO_HELP_PROMPT =
    "I am using the Geodesic / Distance Field tool in 3D Claw.\n\n"
    "Three methods are available:\n"
    "  Front Propagation - Dijkstra-like wavefront expansion over\n"
    "    virtual edges (handles obtuse triangles). Best for teaching,\n"
    "    visual debugging, seed-region exploration. Parameters: virtual\n"
    "    edges on/off, max distance, live preview speed.\n"
    "  Exact Shortest Path (CGAL) - builds a sequence tree then extracts\n"
    "    exact geodesic paths via surface unfolding. Best for accurate\n"
    "    source-to-target measurement. Requires triangle mesh. Output is\n"
    "    a path polyline with exact length.\n"
    "  Heat Method (CGAL) - solves heat diffusion + Poisson equation for\n"
    "    fast all-vertex distance field. Direct variant uses the input\n"
    "    mesh; Intrinsic Delaunay variant uses the intrinsic Delaunay\n"
    "    triangulation for better accuracy on poor-quality meshes.\n\n"
    "Please suggest which method to use, parameter settings, and any\n"
    "warnings for my mesh.";

void append_mesh_metadata(std::ostringstream& oss, easy3d::SurfaceMesh* mesh) {
    const auto m = claw_ai::collect_surface_mesh_ai_stats(mesh);
    oss << "Source mesh:\n";
    oss << "- Name: " << m.name << " | v=" << m.vertices
        << " e=" << m.edges << " f=" << m.faces << "\n";
    oss << "- Triangle: " << (m.triangle_mesh ? "yes" : "no")
        << " | Closed: " << (m.closed ? "yes" : "no") << "\n";
    oss << "- Boundary edges: " << m.boundary_edges
        << " | Non-triangle faces: " << m.non_triangle_faces << "\n";
    oss << "- Components: " << (m.connected_components >= 0
        ? std::to_string(m.connected_components) : "unknown") << "\n";
    oss << std::fixed << std::setprecision(4);
    oss << "- BBox diagonal: " << m.bbox_diag
        << " | Avg edge length: " << m.avg_edge_length << "\n";
    oss.unsetf(std::ios::floatfield);
}

std::string build_geo_advice_prompt(easy3d::SurfaceMesh* mesh,
                                     const GeodesicState& s) {
    std::ostringstream oss;
    oss << "Geodesic / Distance Field - Parameter Advice\n\n";
    append_mesh_metadata(oss, mesh);
    oss << "\nSelected method: " << mode_label(s.mode) << "\n";
    oss << "Sources: " << s.sources.size() << "\n";
    oss << "Target: " << (s.target_valid
        ? std::to_string(s.target_vid) : "none") << "\n";
    if (s.mode == GEO_MODE_FrontPropagation) {
        oss << "Virtual edges: " << (s.use_virtual_edges ? "on" : "off") << "\n";
        oss << "Live preview: " << (s.live_preview ? "on" : "off")
            << " speed=" << s.preview_speed << "\n";
    }
    if (s.mode == GEO_MODE_HeatMethod) {
        oss << "Variant: " << (s.heat_variant == GEO_HEAT_Direct
            ? "Direct" : "Intrinsic Delaunay") << "\n";
    }
    oss << "\nWhich method should I use? Any parameter suggestions or warnings?\n";
    return ascii_only(oss.str());
}

std::string build_geo_evaluation_prompt(const GeodesicState& s,
                                         const GEO_FrontResultStats& st) {
    const int result_mode = s.last_result_mode;
    std::ostringstream oss;
    oss << "Geodesic / Distance Field - Result Evaluation\n\n";
    oss << "Method: " << mode_label(result_mode) << "\n";
    oss << "Sources: " << st.source_count
        << " | Target: " << (s.target_valid ? "yes" : "no") << "\n";
    oss << std::fixed << std::setprecision(4);
    if (result_mode == GEO_MODE_FrontPropagation) {
        oss << "Visited vertices: " << st.visited_vertices
            << " / " << st.input_vertices << "\n";
        oss << "Max distance: " << st.max_distance
            << " | Mean distance: " << st.mean_distance << "\n";
        oss << "Peak front size: " << st.peak_front_size << "\n";
        if (st.path_found)
            oss << "Front path length: " << st.front_path_length << "\n";
        if (st.exact_path_found) {
            oss << "Exact path length: " << st.exact_path_length << "\n";
            if (st.front_path_length > 0.0f) {
                double err = std::abs((double)(st.front_path_length
                    - st.exact_path_length))
                    / (double)st.exact_path_length;
                oss << "Relative error: " << (err * 100.0) << "%\n";
            }
        }
    } else if (result_mode == GEO_MODE_HeatMethod) {
        oss << "Vertices: " << st.visited_vertices << "\n";
        oss << "Max distance: " << st.max_distance
            << " | Mean distance: " << st.mean_distance << "\n";
        oss << "Variant: " << (s.heat_variant == GEO_HEAT_Direct
            ? "Direct" : "Intrinsic Delaunay") << "\n";
    } else if (result_mode == GEO_MODE_ExactShortestPath) {
        if (st.exact_path_found)
            oss << "Exact path length: " << st.exact_path_length << "\n";
    }
    oss << "Runtime: " << st.ms_total << " ms | Cancelled: "
        << (st.cancelled ? "yes" : "no") << "\n";
    oss.unsetf(std::ios::floatfield);
    oss << "\n1. Is this result reliable?\n";
    oss << "2. Does the selected method match my likely use case?\n";
    oss << "3. Should I try a different method or adjust parameters?\n";
    return ascii_only(oss.str());
}

// ============================================================================
// UI helpers
// ============================================================================

bool vertex_position(easy3d::SurfaceMesh* mesh, int vid, easy3d::vec3& out) {
    if (!is_live_vertex(mesh, vid)) return false;
    auto pts = mesh->get_vertex_property<easy3d::vec3>("v:point");
    if (!pts) return false;
    out = pts[easy3d::SurfaceMesh::Vertex(vid)];
    return true;
}

void push_overlays(MainWindow* win,
                   easy3d::SurfaceMesh* mesh,
                   const GeodesicState& s)
{
    if (!win || !mesh) return;
    std::vector<easy3d::vec3> src_pts;
    src_pts.reserve(s.sources.size());
    for (int vid : s.sources) {
        easy3d::vec3 p;
        if (vertex_position(mesh, vid, p)) src_pts.push_back(p);
    }
    win->update_geo_source_overlay(mesh, src_pts);
    if (s.target_valid && s.target_vid >= 0) {
        easy3d::vec3 p;
        if (vertex_position(mesh, s.target_vid, p))
            win->update_geo_target_overlay(mesh, &p);
        else
            win->update_geo_target_overlay(mesh, nullptr);
    } else {
        win->update_geo_target_overlay(mesh, nullptr);
    }
}

} // namespace

void renderDialogGeodesicDistance(ViewportCanvas* viewer, GeodesicState& s, bool& open) {
    prepare_dialog_window(560, 620);
    DIALOG_BODY("Geodesic / Distance Field", open) {
        prereq_hint_only(prereq_surface_mesh(viewer));
        auto* win    = MainWindow::instance();
        render_panel_header(
            "Geodesic distance on a triangle mesh (front propagation / exact path / heat method).",
            "Path planning, isoline visualization, distance-driven coloring.",
            [win]() { claw_ai::send_panel_ai_prompt(
                          win, GEO_HELP_PROMPT,
                          "Ask AI: Geodesic distance help"); });
        const bool geo_busy =
            (s.front_runner && win &&
             win->algorithm_controller().is_running_id(AlgorithmId::GeodesicDistance))
#ifdef CLAW3D_HAS_CGAL
            || (s.exact_runner && win &&
                win->algorithm_controller().is_running_id(AlgorithmId::GeodesicDistance))
#endif
            ;
        if (!open && geo_busy) {
            open = true;
            s.close_requested = true;
            if (s.front_runner) s.front_runner.cancel();
#ifdef CLAW3D_HAS_CGAL
            if (s.exact_runner) s.exact_runner.cancel();
#endif
            glfwPostEmptyEvent();
        }

        auto* viewer = win ? win->viewer() : nullptr;
        auto lock_geo_pick_input = [&]() {
            if (viewer) {
                viewer->input_locked_ = true;
                s.owns_viewport_input_lock = true;
            }
        };
        auto release_geo_pick_input = [&]() {
            if (viewer && s.owns_viewport_input_lock) {
                viewer->input_locked_ = false;
                s.owns_viewport_input_lock = false;
            }
        };

        // Detect mode switch - clear stale overlays from previous mode.
        static int prev_mode = -1;
        if (prev_mode != -1 && prev_mode != s.mode) {
            if (win) {
                win->clear_front_overlay();
            }
        }
        prev_mode = s.mode;

        // Cleanup overlays when dialog closes.
        if (!open) {
            if (win) win->clear_front_overlay();
            s.pick_mode = 0;
            release_geo_pick_input();
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
            release_geo_pick_input();
            ImGui::TextColored(claw_ui::status_error_color(),
                "No surface mesh loaded.");
            if (ImGui::Button("Close")) open = false;
        } else {
#ifdef CLAW3D_HAS_CGAL
            const bool has_cgal = true;
#else
            const bool has_cgal = false;
#endif
            const int nv = (int)mesh->n_vertices();
            const int nf = (int)mesh->n_faces();
            const int vertex_id_capacity = (int)mesh->vertices_size();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (v=%d f=%d) | CGAL: %s", mesh->name().c_str(),
                nv, nf, has_cgal ? "YES" : "NO");
            // Warn for large meshes (front propagation may be slow).
            if (nv > 200000) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Large mesh (%d vertices). "
                    "Front Propagation may be slow; consider Heat Method.", nv);
            }
            ImGui::Spacing();

            const bool busy = win && win->algorithm_controller().is_running();
            if (s.pick_mode != 0 && viewer && !busy)
                lock_geo_pick_input();
            else if (s.owns_viewport_input_lock)
                release_geo_pick_input();

            // Vertex picking handler (one-shot: pick face + adsorb to nearest
            // vertex). Runs before the source/target UI so pick result is
            // applied within the same frame.
            if (s.pick_mode != 0 && viewer && !busy) {
                auto& io = ImGui::GetIO();
                const bool clicked = ImGui::IsMouseClicked(0);
                const bool in_vp = viewer->is_in_viewport(io.MousePos.x, io.MousePos.y);
                if (clicked && in_vp)
                {
                    const float local_x = io.MousePos.x - viewer->viewport_min_x();
                    const float local_y = io.MousePos.y - viewer->viewport_min_y();
                    int best_vid = -1;
                    const bool picked_vertex = viewer->pick_surface_vertex(
                        mesh, (int)local_x, (int)local_y, best_vid);

                    if (picked_vertex) {
                        if (best_vid >= 0) {
                            if (s.pick_mode == 1) {
                                if (std::find(s.sources.begin(), s.sources.end(),
                                              best_vid) == s.sources.end()) {
                                    s.sources.push_back(best_vid);
                                    push_overlays(win, mesh, s);
                                    std::snprintf(s.pick_status,
                                        sizeof(s.pick_status),
                                        "Source %d picked", best_vid);
                                } else {
                                    std::snprintf(s.pick_status,
                                        sizeof(s.pick_status),
                                        "Vertex %d already in sources", best_vid);
                                }
                            } else {
                                s.target_vid = best_vid;
                                s.target_valid = true;
                                push_overlays(win, mesh, s);
                                std::snprintf(s.pick_status,
                                    sizeof(s.pick_status),
                                    "Target %d picked", best_vid);
                            }
                        } else {
                            std::snprintf(s.pick_status, sizeof(s.pick_status),
                                "No vertex picked (click on the surface)");
                        }
                    } else {
                        std::snprintf(s.pick_status, sizeof(s.pick_status),
                            "No mesh vertex picked (click on the surface)");
                    }
                    s.pick_mode = 0;
                    release_geo_pick_input();
                } else if (clicked && !in_vp) {
                    s.pick_mode = 0;
                    release_geo_pick_input();
                    std::snprintf(s.pick_status, sizeof(s.pick_status),
                        "Clicked outside viewport");
                }
            }

            // Method selector.
            const char* methods[] = {
                "Front Propagation (Easy3D)",
                "Exact Shortest Path (CGAL)",
                "Heat Method (CGAL)"
            };
            ImGui::Combo("Method", &s.mode, methods, IM_ARRAYSIZE(methods));
            ImGui::TextWrapped("%s", mode_hint(s.mode));
            ImGui::Spacing();
            ImGui::Separator();

            // Source picking (vertex id input + pick button).
            ImGui::Text("Sources (%d)", (int)s.sources.size());
            claw_ui::same_line_if_fits_button("Pick Source");
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("Pick Source")) {
                s.pick_mode = 1;
                s.pick_status[0] = '\0';
                lock_geo_pick_input();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click on the mesh surface to pick a vertex");
            ImGui::PushID("src");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Vertex ID", &s.pending_source_vid, 1, 10);
            if (s.pending_source_vid < 0) s.pending_source_vid = 0;
            if (vertex_id_capacity > 0 && s.pending_source_vid >= vertex_id_capacity)
                s.pending_source_vid = vertex_id_capacity - 1;
            claw_ui::same_line_if_fits_button("Add Source");
            if (ImGui::Button("Add Source") && !busy) {
                int vid = s.pending_source_vid;
                if (is_live_vertex(mesh, vid)) {
                    if (std::find(s.sources.begin(), s.sources.end(), vid)
                            == s.sources.end()) {
                        s.sources.push_back(vid);
                        push_overlays(win, mesh, s);
                    }
                }
            }
            claw_ui::same_line_if_fits_button("Clear Sources");
            if (ImGui::Button("Clear Sources") && !busy) {
                s.sources.clear();
                push_overlays(win, mesh, s);
            }
            if (!s.sources.empty()) {
                std::string line = "  [ ";
                for (std::size_t i = 0; i < s.sources.size() && i < 16; ++i) {
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "%d ", s.sources[i]);
                    line += buf;
                }
                if (s.sources.size() > 16) line += "... ";
                line += "]";
                ImGui::TextDisabled("%s", line.c_str());
            }
            if (s.pick_mode == 1)
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Pick mode active - click on the mesh surface...");
            ImGui::PopID();
            ImGui::Spacing();

            // Target picking.
            ImGui::Text("Target: %s",
                s.target_valid ? std::to_string(s.target_vid).c_str() : "none");
            claw_ui::same_line_if_fits_button("Pick Target");
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("Pick Target")) {
                s.pick_mode = 2;
                s.pick_status[0] = '\0';
                lock_geo_pick_input();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click on the mesh surface to pick a vertex");
            ImGui::PushID("tgt");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Vertex ID", &s.pending_target_vid, 1, 10);
            if (s.pending_target_vid < 0) s.pending_target_vid = 0;
            if (vertex_id_capacity > 0 && s.pending_target_vid >= vertex_id_capacity)
                s.pending_target_vid = vertex_id_capacity - 1;
            claw_ui::same_line_if_fits_button("Set Target");
            if (ImGui::Button("Set Target") && !busy) {
                int vid = s.pending_target_vid;
                if (is_live_vertex(mesh, vid)) {
                    s.target_vid = vid;
                    s.target_valid = true;
                    push_overlays(win, mesh, s);
                }
            }
            claw_ui::same_line_if_fits_button("Clear Target");
            if (ImGui::Button("Clear Target") && !busy) {
                s.target_vid = -1;
                s.target_valid = false;
                push_overlays(win, mesh, s);
            }
            ImGui::PopID();
            if (s.pick_mode == 2)
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Pick mode active - click on the mesh surface...");
            if (s.pick_status[0])
                ImGui::TextColored(claw_ui::status_success_color(),
                    "%s", s.pick_status);
            ImGui::Spacing();
            ImGui::Separator();

            // CGAL modes require triangle mesh.
            const bool is_tri = mesh->is_triangle_mesh();
            const int live_source_count = (int)std::count_if(
                s.sources.begin(), s.sources.end(),
                [mesh](int vid) { return is_live_vertex(mesh, vid); });
            const bool target_live =
                s.target_valid && is_live_vertex(mesh, s.target_vid);
            if ((s.mode == GEO_MODE_ExactShortestPath ||
                 s.mode == GEO_MODE_HeatMethod) && !is_tri) {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "This mode requires a triangle mesh. "
                    "Current mesh has non-triangle faces.");
                ImGui::Spacing();
            }

            // Mode-specific options.
            if (s.mode == GEO_MODE_FrontPropagation) {
                ImGui::Checkbox("Use Virtual Edges", &s.use_virtual_edges);
                ImGui::Checkbox("Live Preview", &s.live_preview);
                if (s.live_preview) {
                    claw_ui::same_line_if_fits_width(220.0f);
                    const char* sp[] = {"Normal", "Slow"};
                    ImGui::Combo("Speed", &s.preview_speed, sp, 2);
                }
#ifdef CLAW3D_HAS_CGAL
                if (target_live)
                    ImGui::Checkbox("Compare Exact Path", &s.compare_exact_path);
                else
                    ImGui::TextDisabled("Compare Exact Path (needs target)");
#endif
            } else if (s.mode == GEO_MODE_HeatMethod) {
                const char* variants[] = {"Direct", "Intrinsic Delaunay"};
                ImGui::Combo("Variant", &s.heat_variant,
                             variants, IM_ARRAYSIZE(variants));
            }
            ImGui::Spacing();

            // Live front polling.
            if (s.mode == GEO_MODE_FrontPropagation && s.front_runner && busy) {
                GEO_FrontSnapshot snap;
                if (s.front_runner.poll_snapshot(s.last_snap_gen, snap)) {
                    if (!snap.vertex_distances.empty() && win) {
                        win->update_front_overlay(snap);
                        s.last_visited      = snap.visited_vertices;
                        s.last_front_size   = snap.front_size;
                        s.last_max_distance = snap.max_distance;
                    }
                }
                glfwPostEmptyEvent();
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Front: visited %d/%d | front_size=%d | max_dist=%.4g",
                    s.last_visited, nv, s.last_front_size,
                    (double)s.last_max_distance);
            }

            // AI Parameter Advice.
            if (!busy) {
                if (ImGui::Button("AI Parameter Advice"))
                    claw_ai::send_panel_ai_prompt(
                        win, build_geo_advice_prompt(mesh, s),
                        "Ask AI: Geodesic parameter advice");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            // Run / Cancel buttons.
            const bool can_run_front =
                (s.mode == GEO_MODE_FrontPropagation) && live_source_count > 0;
            const bool can_run_exact =
                (s.mode == GEO_MODE_ExactShortestPath) && live_source_count > 0
                && target_live && is_tri;
            const bool can_run_heat =
                (s.mode == GEO_MODE_HeatMethod) && live_source_count > 0 && is_tri;
            const bool can_run = can_run_front || can_run_exact || can_run_heat;
            if (!busy) {
                ImGui::BeginDisabled(!can_run);
                if (ImGui::Button("Run") && can_run) {
                    s.last_result_mode = s.mode;
                    if (win)
                        win->clear_geo_path_overlays();
                    if (s.mode == GEO_MODE_FrontPropagation) {
                        s.last_stats_valid = false;
                        s.last_snap_gen = -1;
                        s.last_visited = 0;
                        s.last_front_size = 0;
                        s.last_max_distance = 0.0f;
                        s.close_requested = false;
                        s.settling = false;
                        s.last_error.clear();

                        GEO_FrontConfig cfg;
                        cfg.use_virtual_edges = s.use_virtual_edges;
                        cfg.preview_speed = s.preview_speed;
                        cfg.snapshot_min_ms =
                            (s.preview_speed == 1) ? 200 : 50;
                        cfg.max_dist = 1e30f;
                        cfg.target_vid =
                            s.target_valid ? s.target_vid : -1;
                        cfg.compare_exact_path = s.compare_exact_path;

                        if (s.live_preview && win)
                            win->init_front_overlay(mesh);

                        claw3d::services::GeodesicFrontJobStart request;
                        request.source_mesh = mesh;
                        request.source_handle =
                            (win && win->viewer())
                                ? win->viewer()->model_handle(mesh)
                                : ModelHandle{};
                        request.source_vertex_ids = s.sources;
                        request.config = cfg;
                        request.final_result_ready = &s.final_result_ready;
                        request.wake_ui = []() { glfwPostEmptyEvent(); };

                        s.front_runner =
                            claw3d::services::start_geodesic_front_job(
                                win->algorithm_controller(), request);
                        if (!s.front_runner) {
                            if (s.live_preview && win)
                                win->clear_front_overlay();
                            s.final_result_ready.store(
                                true, std::memory_order_release);
                            s.last_error =
                                "Failed to start geodesic front job.";
                            LOG(WARNING) << s.last_error;
                        }
                    }
#ifdef CLAW3D_HAS_CGAL
                    else if (s.mode == GEO_MODE_ExactShortestPath) {
                        s.exact_result_valid = false;
                        s.exact_path_result = GEO_CGAL_PathResult{};
                        s.exact_final_ready.store(false,
                            std::memory_order_release);
                        s.last_stats_valid = false;
                        s.last_error.clear();

                        claw3d::services::GeodesicExactPathJobStart request;
                        request.source_mesh = mesh;
                        request.source_handle =
                            (win && win->viewer())
                                ? win->viewer()->model_handle(mesh)
                                : ModelHandle{};
                        request.source_vertex_id = s.sources.empty()
                            ? -1
                            : s.sources[0];
                        request.target_vertex_id = s.target_vid;
                        request.path_result = &s.exact_path_result;
                        request.result_valid = &s.exact_result_valid;
                        request.final_result_ready = &s.exact_final_ready;
                        request.wake_ui = []() { glfwPostEmptyEvent(); };

                        s.exact_runner =
                            claw3d::services::start_geodesic_exact_path_job(
                                win->algorithm_controller(), request);
                        if (!s.exact_runner) {
                            s.exact_final_ready.store(
                                true, std::memory_order_release);
                            s.last_error =
                                "Failed to start exact geodesic job.";
                            LOG(WARNING) << s.last_error;
                        }
                    }
#endif
#ifdef CLAW3D_HAS_CGAL
                    else if (s.mode == GEO_MODE_HeatMethod) {
                        s.exact_result_valid = false;
                        s.exact_final_ready.store(false,
                            std::memory_order_release);
                        s.last_stats_valid = false;
                        s.last_error.clear();

                        claw3d::services::GeodesicHeatMethodJobStart request;
                        request.source_mesh = mesh;
                        request.source_handle =
                            (win && win->viewer())
                                ? win->viewer()->model_handle(mesh)
                                : ModelHandle{};
                        request.source_vertex_ids = s.sources;
                        request.heat_variant = s.heat_variant;
                        request.result_valid = &s.exact_result_valid;
                        request.final_result_ready = &s.exact_final_ready;
                        request.wake_ui = []() { glfwPostEmptyEvent(); };

                        s.exact_runner =
                            claw3d::services::start_geodesic_heat_method_job(
                                win->algorithm_controller(), request);
                        if (!s.exact_runner) {
                            s.exact_final_ready.store(
                                true, std::memory_order_release);
                            s.last_error =
                                "Failed to start heat geodesic job.";
                            LOG(WARNING) << s.last_error;
                        }
                    }
#endif
                }
                ImGui::EndDisabled();
                if (!can_run) {
                    claw_ui::same_line_if_fits_width(260.0f);
                    ImGui::TextDisabled(
                        s.mode == GEO_MODE_FrontPropagation
                            ? "Pick at least one source vertex"
                            : (s.mode == GEO_MODE_ExactShortestPath
                                ? "Pick at least one source and one target"
                                : (s.mode == GEO_MODE_HeatMethod
                                    ? "Pick at least one source vertex"
                                    : "Not available yet")));
                }
            } else {
                bool has_active_runner = static_cast<bool>(s.front_runner);
#ifdef CLAW3D_HAS_CGAL
                has_active_runner = has_active_runner || static_cast<bool>(s.exact_runner);
#endif
                if (has_active_runner && ImGui::Button("Cancel")) {
                    if (s.front_runner) s.front_runner.cancel();
#ifdef CLAW3D_HAS_CGAL
                    if (s.exact_runner) s.exact_runner.cancel();
#endif
                    glfwPostEmptyEvent();
                }
                if (((s.front_runner && s.front_runner.is_cancelled())
#ifdef CLAW3D_HAS_CGAL
                     || (s.exact_runner && s.exact_runner.is_cancelled())
#endif
                     ) && !s.close_requested)
                {
                    claw_ui::same_line_if_fits_text("Cancel requested...");
                    ImGui::TextDisabled("Cancel requested...");
                }
                if (s.close_requested) {
                    claw_ui::same_line_if_fits_text("Closing after worker stops...");
                    ImGui::TextDisabled("Closing after worker stops...");
                }
            }

            // Worker completion -> optional settle, then handoff.
            if (s.front_runner && busy && s.front_runner.is_done() &&
                s.final_result_ready.load(std::memory_order_acquire))
            {
                const bool close_after = s.close_requested;
                const bool finish_immediately =
                    !s.live_preview || close_after ||
                    s.front_runner.is_cancelled() ||
                    s.front_runner.has_error();
                if (finish_immediately) {
                    if (s.live_preview && win)
                        win->clear_front_overlay();
                    if (s.front_runner.has_error())
                        s.last_error = s.front_runner.last_error();
                    s.last_stats = s.front_runner.result_stats();
                    s.last_stats_valid = true;
                    mark_algorithm_done(win);
                    s.front_runner.reset();
                    s.settling = false;
                    s.close_requested = false;
                    if (close_after) open = false;
                    glfwPostEmptyEvent();
                } else if (!s.settling) {
                    GEO_FrontSnapshot snap;
                    if (s.front_runner.poll_snapshot(s.last_snap_gen, snap) &&
                        win && !snap.vertex_distances.empty())
                    {
                        win->update_front_overlay(snap);
                    }
                    if (s.front_runner.has_error())
                        s.last_error = s.front_runner.last_error();
                    s.last_stats = s.front_runner.result_stats();
                    s.last_stats_valid = true;
                    // Paint the front backtrace path if one was found.
                    if (s.last_stats.path_found && win) {
                        std::vector<float> xyz;
                        s.front_runner.get_front_path(xyz);
                        win->update_geo_front_path_overlay(mesh, xyz);
                    }
                    // Paint the CGAL exact path if one was found.
                    if (s.last_stats.exact_path_found && win) {
                        std::vector<float> xyz;
                        s.front_runner.get_exact_path(xyz);
                        win->update_geo_exact_path_overlay(mesh, xyz);
                    }
                    s.settling = true;
                    s.settle_started_at = ImGui::GetTime();
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Done. Holding distance field for %.1fs ...",
                        s.settle_ms / 1000.0);
                } else {
                    const double now = ImGui::GetTime();
                    const double elapsed =
                        (now - s.settle_started_at) * 1000.0;
                    if (elapsed >= s.settle_ms) {
                        if (win) win->clear_front_overlay();
                        mark_algorithm_done(win);
                        s.front_runner.reset();
                        s.settling = false;
                        s.close_requested = false;
                        glfwPostEmptyEvent();
                    } else {
                        ImGui::TextColored(claw_ui::status_success_color(),
                            "Done. Holding distance field %.1fs / %.1fs ...",
                            elapsed / 1000.0, s.settle_ms / 1000.0);
                    }
                }
            }

            // Exact Shortest Path / Heat Method running / completion.
#ifdef CLAW3D_HAS_CGAL
            const std::string active_geo_label =
                win ? win->algorithm_controller().current_label() : std::string();
            const bool exact_job_busy = s.exact_runner && busy && win &&
                (active_geo_label == "Geodesic Exact Shortest Path" ||
                 active_geo_label == "Geodesic Heat Method");
            const bool heat_job =
                win && active_geo_label == "Geodesic Heat Method";
            if (exact_job_busy)
            {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "%s",
                    heat_job
                        ? "Computing Heat Method..."
                        : "Computing exact shortest path...");
                glfwPostEmptyEvent();
            }
            if (exact_job_busy && s.exact_runner.is_done() &&
                s.exact_final_ready.load(std::memory_order_acquire))
            {
                const bool close_after = s.close_requested;
                const int result_mode = heat_job
                    ? GEO_MODE_HeatMethod
                    : GEO_MODE_ExactShortestPath;
                s.last_result_mode = result_mode;
                s.exact_result_valid = true;
                if (s.exact_runner.has_error())
                    s.last_error = s.exact_runner.last_error();

                if (result_mode == GEO_MODE_HeatMethod) {
                    auto heat_res = s.exact_runner.heat_result();
                    if (!close_after && !heat_res.vertex_distances.empty() && mesh) {
                        // Apply heatmap directly on the source mesh so this
                        // mode does not depend on the front-overlay lifecycle.
                        auto* fd = mesh->renderer()->get_triangles_drawable("faces");
                        if (fd) {
                            auto oldp = mesh->get_vertex_property<float>("v:geo_heat");
                            if (oldp) mesh->remove_vertex_property(oldp);
                            auto hprop = mesh->add_vertex_property<float>(
                                "v:geo_heat", 0.0f);
                            int i = 0;
                            for (auto v : mesh->vertices()) {
                                if (i < (int)heat_res.vertex_distances.size())
                                    hprop[v] = (float)heat_res.vertex_distances[i];
                                ++i;
                            }
                            fd->set_scalar_coloring(
                                easy3d::State::VERTEX, "v:geo_heat",
                                nullptr, 0.02f, 0.02f);
                            fd->update();
                        }
                    }
                    s.last_stats_valid = true;
                    s.last_stats = GEO_FrontResultStats{};
                    s.last_stats.input_vertices =
                        (int)heat_res.vertex_distances.size();
                    s.last_stats.input_faces = nf;
                    s.last_stats.source_count = (int)s.sources.size();
                    s.last_stats.visited_vertices =
                        (int)heat_res.vertex_distances.size();
                    s.last_stats.max_distance = (float)heat_res.max_distance;
                    s.last_stats.mean_distance = (float)heat_res.mean_distance;
                    s.last_stats.cancelled = heat_res.cancelled;
                    s.last_stats.ms_total = heat_res.ms_total;
                } else if (!close_after && s.exact_path_result.path_found && win) {
                    std::vector<float> xyz;
                    xyz.reserve(s.exact_path_result.points.size() * 3);
                    for (const auto& p : s.exact_path_result.points) {
                        xyz.push_back((float)p.x);
                        xyz.push_back((float)p.y);
                        xyz.push_back((float)p.z);
                    }
                    win->update_geo_exact_path_overlay(mesh, xyz);
                    s.last_stats_valid = true;
                    s.last_stats = GEO_FrontResultStats{};
                    s.last_stats.input_vertices = nv;
                    s.last_stats.input_faces = nf;
                    s.last_stats.source_count = (int)s.sources.size();
                    s.last_stats.exact_path_found = true;
                    s.last_stats.exact_path_length = (float)s.exact_path_result.length;
                    s.last_stats.cancelled = s.exact_runner.is_cancelled();
                    s.last_stats.ms_total = s.exact_path_result.ms_build_tree
                        + s.exact_path_result.ms_query;
                }
                mark_algorithm_done(win);
                s.exact_runner.reset();
                s.close_requested = false;
                if (close_after) open = false;
                glfwPostEmptyEvent();
            }
#endif

            // Post-run stats.
            if (s.last_stats_valid && !busy) {
                const auto& st = s.last_stats;
                const int result_mode = s.last_result_mode;
                ImGui::Spacing();
                if (result_mode == GEO_MODE_ExactShortestPath) {
#ifdef CLAW3D_HAS_CGAL
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Exact shortest path: src=%d -> tgt=%d",
                        s.sources.empty() ? -1 : s.sources[0],
                        s.target_vid);
                    if (st.exact_path_found) {
                        ImGui::TextColored(claw_ui::status_running_color(),
                            "Path length: %.4g | points=%zu",
                            (double)st.exact_path_length,
                            s.exact_path_result.points.size());
                        ImGui::TextColored(claw_ui::status_muted_color(),
                            "Build: %.1f ms | Query: %.1f ms",
                            s.exact_path_result.ms_build_tree,
                            s.exact_path_result.ms_query);
                    } else {
                        ImGui::TextDisabled("No path found.");
                    }
#else
                    ImGui::TextDisabled("Exact shortest path requires CGAL.");
#endif
                } else if (result_mode == GEO_MODE_HeatMethod) {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Heat Method (%s): %d source(s)",
                        s.heat_variant == GEO_HEAT_Direct
                            ? "Direct" : "Intrinsic Delaunay",
                        st.source_count);
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Min: %.4g | Max: %.4g | Mean: %.4g",
                        (double)st.max_distance > 0
                            ? 0.0 : (double)st.max_distance,
                        (double)st.max_distance,
                        (double)st.mean_distance);
                    ImGui::TextColored(claw_ui::status_muted_color(),
                        "Vertices: %d | %.1f ms",
                        st.visited_vertices, st.ms_total);
                } else {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Result: %d visited / %d | max=%.4g mean=%.4g | "
                        "peak_front=%d | %.0f ms",
                        st.visited_vertices, st.input_vertices,
                        (double)st.max_distance, (double)st.mean_distance,
                        st.peak_front_size, st.ms_total);
                    if (st.path_found) {
                        ImGui::TextColored(claw_ui::status_running_color(),
                            "Front path length: %.4g",
                            (double)st.front_path_length);
                    } else if (s.target_valid) {
                        ImGui::TextDisabled(
                            "No path back to source (target unreachable?)");
                    }
                    if (st.exact_path_found) {
                        ImGui::TextColored(claw_ui::status_running_color(),
                            "Exact path length: %.4g",
                            (double)st.exact_path_length);
                        if (st.path_found && st.front_path_length > 0) {
                            double err = std::abs((double)(st.front_path_length -
                                st.exact_path_length)) / (double)st.exact_path_length;
                            ImGui::TextColored(claw_ui::status_warning_color(),
                                "Relative error: %.2f%% (%s)",
                                err * 100.0,
                                err < 0.05 ? "good" : err < 0.10 ? "ok" : "check topology");
                        }
                    }
                }
                if (st.cancelled)
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Status: cancelled");
                if (!s.last_error.empty())
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "Error: %s", s.last_error.c_str());
            }

            // AI Evaluate Result - available when results exist.
            if (s.last_stats_valid && !busy) {
                if (ImGui::Button("AI Evaluate Result"))
                    claw_ai::send_panel_ai_prompt(
                        win, build_geo_evaluation_prompt(s, s.last_stats),
                        "Ask AI: evaluate Geodesic result");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            if (ImGui::Button("Close Panel")) {
                bool has_active_runner = static_cast<bool>(s.front_runner);
#ifdef CLAW3D_HAS_CGAL
                has_active_runner = has_active_runner || static_cast<bool>(s.exact_runner);
#endif
                if (busy && has_active_runner) {
                    s.close_requested = true;
                    if (s.front_runner) s.front_runner.cancel();
#ifdef CLAW3D_HAS_CGAL
                    if (s.exact_runner) s.exact_runner.cancel();
#endif
                    glfwPostEmptyEvent();
                } else {
                    open = false;
                }
            }
        }
    } DIALOG_END;
}
