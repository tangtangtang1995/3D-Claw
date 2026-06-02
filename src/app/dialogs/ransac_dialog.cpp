// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/ransac_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "common/primitive_preview_policy.h"
#include "platform/window_events.h"
#include "services/jobs/cgal/ransac_detection_job.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "overlays/overlay_controller.h"
#include "window/window_helpers.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/surface_mesh_builder.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/state.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

// =============================================================================
// RANSAC Primitive Extraction
// =============================================================================
static const char* RANSAC_HELP_PROMPT = R"(
Explain Efficient RANSAC (Schnabel et al. 2007, CGAL) for primitive detection:

**Algorithm**: Random sample consensus for shape detection in point clouds.
Iteratively samples minimal point sets, fits candidate primitives, evaluates
inlier support, and extracts the largest connected component.

**Parameters**:
- epsilon: max distance from point to primitive for inlier classification.
  Small epsilon (0.001-0.01): tight fit, fewer inliers per shape, more shapes.
  Large epsilon (0.05-0.2): loose fit, more inliers, fewer larger shapes.
  Typical range: 0.005-0.05. Auto (<0) = 1% of bbox diagonal.
- normal_threshold: maximum normal deviation, cos(angle). Range 0.0-1.0.
  0.9 = ~25 deg allowed deviation. 0.7 = ~45 deg. Lower = stricter.
  Typical: 0.85-0.95 for clean data, 0.7-0.8 for noisy data.
- cluster_epsilon: max distance between connected component points.
  Smaller = tighter clusters. Larger = merge close shapes.
  Auto (<0) = same as epsilon.
- min_points: minimum inliers to accept a shape. Smaller = more shapes
  (may include noise). Larger = fewer, higher-quality shapes.
  Auto (<0) = 1% of total points.

**Tuning strategy**:
- If too many small shapes: increase epsilon and/or min_points, decrease
  cluster_epsilon.
- If shapes are merged: decrease epsilon and cluster_epsilon.
- If normals are noisy: lower normal_threshold (0.7-0.8).
- For large point clouds (>100K): increase min_points, increase epsilon.
- For clean CAD data: epsilon 0.01, normal_threshold 0.9, min_points 500.
)";

static std::string build_parameter_advice_prompt(easy3d::PointCloud* cloud,
                                                 const RansacState& s) {
    std::ostringstream prompt;
    prompt << "Please give Efficient RANSAC primitive extraction parameter "
              "advice for the current point cloud.\n\n";
    if (cloud) {
        prompt << "| Field | Value |\n|---|---|\n";
        prompt << "| Model | " << cloud->name() << " |\n";
        prompt << "| Points | " << cloud->n_vertices() << " |\n";
        const auto& bbox = cloud->bounding_box();
        if (bbox.is_valid())
            prompt << "| BBox diagonal | " << bbox.diagonal_length() << " |\n";
        prompt << "| Has normals | "
               << (cloud->get_vertex_property<easy3d::vec3>("v:normal")
                       ? "yes" : "no")
               << " |\n";
    }
    prompt << "| Parameter | Current value |\n|---|---:|\n";
    prompt << "| Epsilon | " << s.epsilon << " |\n";
    prompt << "| Normal Threshold | " << s.normal_threshold << " |\n";
    prompt << "| Cluster Epsilon | " << s.cluster_epsilon << " |\n";
    prompt << "| Min Points | " << s.min_points << " |\n";
    prompt << "| Live Preview | " << (s.live_preview ? "on" : "off") << " |\n";
    prompt << "| Throttle | " << (s.live_throttle ? "on" : "off") << " |\n\n";
    prompt << RANSAC_HELP_PROMPT << "\n" << ai_lang::directive();
    return prompt.str();
}

void renderDialogRansac(ViewportCanvas* viewer, RansacState& s, bool& open) {
    prepare_dialog_window(520, 580);
    DIALOG_BODY("RANSAC Primitive Extraction", open) {
        prereq_hint_only(prereq_pc_with_normals(viewer));
        // "?" AI help button
        claw_ui::same_line_right_if_fits_button("?");
        if (ImGui::SmallButton("?")) {
            auto* win = MainWindow::instance();
            if (win && win->ai_chat() && win->ai_chat()->HasApiKey()) {
                win->show_ai_chat();
                win->ai_chat()->SendUserMessage(RANSAC_HELP_PROMPT,
                    "Ask AI: RANSAC primitive extraction help");
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ask AI about Efficient RANSAC parameters");

        ImGui::Spacing();
        auto* win = MainWindow::instance();
        auto* viewer = win ? win->viewer() : nullptr;

        // Pick a PointCloud: prefer current_model if it IS one; otherwise
        // scan the model list. After a RANSAC run the current_model auto-
        // switches to the last-added plane patch (a SurfaceMesh) which would
        // make this dialog report "no point cloud" and skip Run, even though
        // the input cloud is still in the scene.
        easy3d::PointCloud* cloud = nullptr;
        if (viewer) {
            cloud = dynamic_cast<easy3d::PointCloud*>(viewer->current_model());
            if (!cloud) {
                for (auto& mp : viewer->models()) {
                    if (auto* pc = dynamic_cast<easy3d::PointCloud*>(mp.get())) {
                        cloud = pc;
                        break;
                    }
                }
            }
        }

        // Single-exit dialog body: no `return` inside DIALOG_BODY -- otherwise
        // ImGui::End() (DIALOG_END) is skipped and ImGui complains
        // "Missing End()" on every subsequent frame.
        bool can_run = false;
        if (!cloud) {
            ImGui::TextColored(claw_ui::status_error_color(),
                "No point cloud loaded. Load a point cloud first.");
            if (ImGui::Button("Close")) open = false;
        } else {
            auto normals = cloud->get_vertex_property<easy3d::vec3>("v:normal");
            bool has_normals = normals && cloud->n_vertices() > 0;
            int n_pts = cloud->n_vertices();
            ImGui::TextColored(claw_ui::status_muted_color(),
                "Input: %s (%d pts)", cloud->name().c_str(), n_pts);
            if (!has_normals) {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "No normals! Run Point Cloud > Normal Estimation first.");
                if (ImGui::Button("Close")) open = false;
            } else {
                ImGui::TextColored(claw_ui::status_success_color(), "Normals: OK");
                can_run = true;
            }
        }
        if (can_run) {
        int n_pts = cloud->n_vertices();

        ImGui::InputFloat("Epsilon", &s.epsilon, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inlier distance threshold. <0 = auto (1%% bbox diag)");
        ImGui::InputFloat("Normal Thresh", &s.normal_threshold, 0.01f, 0.1f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Normal deviation cos(theta) threshold. 0.9 = ~25 deg");
        ImGui::InputFloat("Cluster Epsilon", &s.cluster_epsilon, 0.001f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Connected-component cluster distance. <0 = auto");
        ImGui::InputInt("Min Points", &s.min_points);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum inliers per shape. <0 = auto (1%% of points)");
        ImGui::Spacing();

        ImGui::Checkbox("Live Preview", &s.live_preview);
        claw_ui::same_line_if_fits_text("(?)");
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Real-time visualization during extraction.");
        if (s.live_preview) {
            claw_ui::same_line_if_fits_text("Throttle");
            ImGui::Checkbox("Throttle", &s.live_throttle);
            claw_ui::same_line_if_fits_text("(?)");
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ON: sample every Nth event (faster).\n"
                                  "OFF: show every single attempt (slower).\n"
                                  "Pause always shows all regardless.");
        }

        if (win && !win->algorithm_controller().is_running()) {
            if (ImGui::Button("AI Parameter Advice")) {
                if (win->ai_chat() && win->ai_chat()->HasApiKey()) {
                    win->send_ai_request(
                        build_parameter_advice_prompt(cloud, s),
                        AICtx_CurrentModel | AICtx_ActivePanel,
                        std::string(),
                        "Ask AI: RANSAC parameter advice");
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ask AI for RANSAC thresholds appropriate for this cloud.");
            if (win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                ImGui::TextDisabled("Set API key in AI Chat");
            }
        }

#ifdef CLAW3D_HAS_CGAL
        bool busy = win && win->algorithm_controller().is_running();

        // --- Live event drain ---
        // Each frame (during AND after a run), pull ShapeAccepted events
        // from the runner's queue and incrementally add plane-patch models
        // to the scene. Patches built here are the ONLY ones added in
        // live mode - the worker's post-run loop is skipped for live to
        // avoid duplicates.
        auto drain_live = [&]() {
            auto run = s.runner;
            if (!run) return;
            std::vector<RANSAC_FrameEvent> events;
            if (!run.drain_live_events(events)) return;

            // bbox once per frame (cloud may have moved? unlikely; cached)
            const auto& bb = cloud->bounding_box();
            const float bbox_diag = bb.is_valid() ? bb.diagonal_length() : 1.0f;
            const easy3d::vec3 bbox_center = bb.is_valid() ? bb.center() : easy3d::vec3(0, 0, 0);
            auto& cloud_pts = cloud->get_vertex_property<easy3d::vec3>("v:point").vector();
            const int n_in = (int)cloud_pts.size();

            // Coalesce transient events: pick the LATEST Candidate; it
            // carries both the plane equation AND the 3 sample points that
            // produced it (paired in the visitor). Drive BOTH overlays
            // from this single event so the gold triangle and yellow patch
            // are always from the same iteration. Fallback: if the batch
            // has only Sampling events (no Candidate yet), show the
            // latest Sampling alone.
            const RANSAC_FrameEvent* latest_candidate = nullptr;
            const RANSAC_FrameEvent* latest_sampling  = nullptr;
            for (auto it = events.rbegin(); it != events.rend(); ++it) {
                if (!latest_candidate && it->type == RANSAC_FrameEvent::Candidate)
                    latest_candidate = &(*it);
                if (!latest_sampling && it->type == RANSAC_FrameEvent::Sampling)
                    latest_sampling = &(*it);
                if (latest_candidate) break;  // candidate is enough
            }
            if (latest_candidate && win) {
                win->overlays().update_ransac_samples_overlay(latest_candidate->sample_pts);
                win->overlays().update_ransac_candidate_overlay(
                    latest_candidate->plane_eq,
                    latest_candidate->sample_pts,
                    bbox_diag);
            } else if (latest_sampling && win) {
                win->overlays().update_ransac_samples_overlay(latest_sampling->sample_pts);
            }

            for (const auto& ev : events) {
                if (ev.type != RANSAC_FrameEvent::ShapeAccepted) continue;
                if (ev.shape_id < s.live_shapes_added) continue; // already drawn
                s.live_shapes_added = ev.shape_id + 1;

                double a = ev.plane_eq[0], b = ev.plane_eq[1],
                       c = ev.plane_eq[2], d = ev.plane_eq[3];
                double len2 = a*a + b*b + c*c;
                if (len2 < 1e-20) continue;
                easy3d::vec3 normal((float)a, (float)b, (float)c);
                normal = normalize(normal);
                easy3d::vec3 t1, t2;
                if (std::abs(normal.x) < 0.9f)
                    t1 = normalize(cross(normal, easy3d::vec3(1, 0, 0)));
                else
                    t1 = normalize(cross(normal, easy3d::vec3(0, 1, 0)));
                t2 = normalize(cross(normal, t1));

                const double inv_norm = 1.0 / std::sqrt(len2);
                const double dist_thresh = (s.epsilon > 0)
                    ? static_cast<double>(
                          s.epsilon * claw3d::primitive_preview_policy::kRansacPlanePatchEpsilonMultiplier)
                    : static_cast<double>(
                          bbox_diag * claw3d::primitive_preview_policy::kRansacPlanePatchBboxRatio);

                double bcx = bbox_center.x, bcy = bbox_center.y, bcz = bbox_center.z;
                double dist_center = (a*bcx + b*bcy + c*bcz + d) * inv_norm;
                easy3d::vec3 ref_center = bbox_center - normal * (float)dist_center;

                double cxa = 0, cya = 0, cza = 0;
                float u_min = std::numeric_limits<float>::max();
                float u_max = -std::numeric_limits<float>::max();
                float v_min = u_min, v_max = u_max;
                int found = 0;
                for (int i = 0; i < n_in; ++i) {
                    double px = cloud_pts[i].x, py = cloud_pts[i].y, pz = cloud_pts[i].z;
                    double dist = std::abs(a*px + b*py + c*pz + d) * inv_norm;
                    if (dist > dist_thresh) continue;
                    cxa += px; cya += py; cza += pz;
                    easy3d::vec3 diff(
                        (float)px - ref_center.x,
                        (float)py - ref_center.y,
                        (float)pz - ref_center.z);
                    float u = dot(diff, t1), v = dot(diff, t2);
                    if (u < u_min) u_min = u; if (u > u_max) u_max = u;
                    if (v < v_min) v_min = v; if (v > v_max) v_max = v;
                    ++found;
                }

                easy3d::vec3 center;
                float half_u, half_v;
                if (found > 8) {
                    center = easy3d::vec3((float)(cxa/found), (float)(cya/found), (float)(cza/found));
                    double dc = (a*(cxa/found) + b*(cya/found) + c*(cza/found) + d) * inv_norm;
                    center = center - normal * (float)dc;
                    half_u = std::max(0.05f * bbox_diag, 0.55f * (u_max - u_min));
                    half_v = std::max(0.05f * bbox_diag, 0.55f * (v_max - v_min));
                } else {
                    center = ref_center;
                    int total_pts = run.total_points();
                    float frac = total_pts > 0
                        ? std::min(1.0f, (float)ev.inlier_count / (float)total_pts * 4.0f)
                        : 0.5f;
                    half_u = half_v = bbox_diag * (0.05f + 0.30f * frac);
                }

                easy3d::vec3 c0 = center - t1*half_u - t2*half_v;
                easy3d::vec3 c1 = center + t1*half_u - t2*half_v;
                easy3d::vec3 c2 = center + t1*half_u + t2*half_v;
                easy3d::vec3 c3 = center - t1*half_u + t2*half_v;

                auto* patch = new easy3d::SurfaceMesh;
                easy3d::SurfaceMeshBuilder bld(patch);
                bld.begin_surface();
                auto vh0 = bld.add_vertex(c0); auto vh1 = bld.add_vertex(c1);
                auto vh2 = bld.add_vertex(c2); auto vh3 = bld.add_vertex(c3);
                bld.add_triangle(vh0, vh1, vh2);
                bld.add_triangle(vh0, vh2, vh3);
                bld.end_surface(false);
                char nm[64]; snprintf(nm, sizeof(nm), "plane_%d", ev.shape_id);
                patch->set_name(nm);

                // Deterministic color from shape_id, shared with this plane's
                // _ch / _as siblings below (and post-run path in main_window).
                easy3d::vec3 col = ransac_shape_color(ev.shape_id);
                auto color_prop = patch->add_face_property<easy3d::vec3>("f:color", col);
                for (auto f : patch->faces()) color_prop[f] = col;

                // Main thread is safe to call add_model() / renderer setup
                auto* saved_current = viewer->current_model();
                viewer->add_model(patch);
                patch->renderer()->set_visible(false); // hide plane, show AS only
                // Register in model tree as child of the source point cloud
                const auto* ci = viewer->model_tree_info(cloud);
                std::string ws = ci ? ci->workspace_name : "Default";
                viewer->register_model_tree_node(patch, ModelTreeNodeInfo{
                    ws, nm, cloud, ModelTreeNodeKind::Primitive, true});

                // --- Convex Hull + Alpha Shape sub-nodes ---
#ifdef CLAW3D_HAS_CGAL
                {
                    // Collect inlier samples for CH/AS (cap at 5000)
                    std::vector<easy3d::vec3> samples;
                    samples.reserve(std::min(
                        found,
                        claw3d::primitive_preview_policy::kRansacDialogInlierSampleLimit));
                    const int step =
                        (found > claw3d::primitive_preview_policy::kRansacDialogInlierSampleLimit)
                            ? (found / claw3d::primitive_preview_policy::kRansacDialogInlierSampleLimit)
                            : 1;
                    int cnt = 0;
                    for (int i = 0;
                         i < n_in &&
                         (int)samples.size() <
                             claw3d::primitive_preview_policy::kRansacDialogInlierSampleLimit;
                         ++i) {
                        double px = cloud_pts[i].x, py = cloud_pts[i].y, pz = cloud_pts[i].z;
                        double d2 = std::abs(a*px + b*py + c*pz + d) * inv_norm;
                        if (d2 > dist_thresh) continue;
                        if (cnt++ % step == 0)
                            samples.push_back({(float)px, (float)py, (float)pz});
                    }

                    if (samples.size() >= 3) {
                        double eq[4] = {a, b, c, d};
                        std::vector<double> flat; flat.reserve(samples.size()*3);
                        for (auto& s : samples) { flat.push_back(s.x); flat.push_back(s.y); flat.push_back(s.z); }

                        // Convex Hull via DLL
                        std::vector<RANSAC_Point3d> ch;
                        claw3d::services::compute_ransac_convex_hull_2d(
                            flat.data(), (int)samples.size(), eq, ch);
                        // Convex Hull -> filled SurfaceMesh via fan triangulation
                        // (CGAL returns CH vertices in CCW order, so the fan
                        // is always planar and well-oriented).
                        if (ch.size() >= 3) {
                            auto* g = new easy3d::SurfaceMesh;
                            g->set_name((std::string(nm) + ".ch").c_str());
                            easy3d::SurfaceMeshBuilder cbld(g);
                            cbld.begin_surface();
                            std::vector<easy3d::SurfaceMesh::Vertex> vh(ch.size());
                            for (size_t i = 0; i < ch.size(); ++i)
                                vh[i] = cbld.add_vertex(easy3d::vec3((float)ch[i].x, (float)ch[i].y, (float)ch[i].z));
                            for (size_t i = 1; i + 1 < ch.size(); ++i)
                                cbld.add_triangle(vh[0], vh[i], vh[i+1]);
                            cbld.end_surface(false);
                            viewer->add_model(g);
                            g->renderer()->set_visible(false); // hide CH, show AS only
                            viewer->register_model_tree_node(g, ModelTreeNodeInfo{ws, g->name(), patch, ModelTreeNodeKind::Primitive, true});
                            easy3d::vec4 col4((float)col.x, (float)col.y, (float)col.z, 1.0f);
                            // Plain unlit single-sided - same as AS / plane.
                            auto* fd = g->renderer()->get_triangles_drawable("faces", false);
                            if (fd) {
                                fd->set_uniform_coloring(col4);
                                fd->set_opacity(1.0f);
                                fd->set_lighting(false);
                                fd->set_distinct_back_color(false);
                            }
                            auto* ld = g->renderer()->get_lines_drawable("edges", false);
                            if (ld) { ld->set_uniform_coloring(col4); ld->set_line_width(1.0f); }
                            auto* vd = g->renderer()->get_points_drawable("vertices", false);
                            if (vd) { vd->set_uniform_coloring(col4); vd->set_point_size(3.0f); }
                        }

                        // Alpha Shape via DLL - filled SurfaceMesh
                        std::vector<RANSAC_Point3d> as_verts;
                        std::vector<std::array<int,3>> as_tris;
                        claw3d::services::compute_ransac_alpha_shape_2d(
                            flat.data(),
                            (int)samples.size(),
                            eq,
                            bbox_diag * claw3d::primitive_preview_policy::kRansacPlanePatchBboxRatio,
                            as_verts,
                            as_tris);
                        if (!as_verts.empty() && !as_tris.empty()) {
                            auto* m = new easy3d::SurfaceMesh;
                            m->set_name((std::string(nm) + ".as").c_str());
                            easy3d::SurfaceMeshBuilder bld(m);
                            bld.begin_surface();
                            std::vector<easy3d::SurfaceMesh::Vertex> vh(as_verts.size());
                            for (size_t i = 0; i < as_verts.size(); ++i)
                                vh[i] = bld.add_vertex(easy3d::vec3((float)as_verts[i].x, (float)as_verts[i].y, (float)as_verts[i].z));
                            for (auto& t : as_tris)
                                bld.add_triangle(vh[t[0]], vh[t[1]], vh[t[2]]);
                            bld.end_surface(false);
                            viewer->add_model(m);
                            viewer->register_model_tree_node(m, ModelTreeNodeInfo{ws, m->name(), patch, ModelTreeNodeKind::Primitive, true});
                            easy3d::vec4 col4((float)col.x, (float)col.y, (float)col.z, 1.0f);
                            // Plain unlit single-sided flat mesh (see comment near
                            // patch faces below).
                            auto* fd = m->renderer()->get_triangles_drawable("faces", false);
                            if (fd) {
                                fd->set_uniform_coloring(col4);
                                fd->set_opacity(1.0f);
                                fd->set_lighting(false);
                                fd->set_distinct_back_color(false);
                            }
                            auto* ed = m->renderer()->get_lines_drawable("edges", false);
                            if (ed) { ed->set_uniform_coloring(col4); ed->set_line_width(1.0f); }
                            auto* vd2 = m->renderer()->get_points_drawable("vertices", false);
                            if (vd2) { vd2->set_uniform_coloring(col4); vd2->set_point_size(3.0f); }
                        }
                    }
                }
#endif

                // Plain flat-shaded mesh: no lighting, no distinct back-face
                // color, full opacity. Matches user request for plain unlit
                // mesh rendering - same setup applies to AS faces above.
                auto* fd = patch->renderer()->get_triangles_drawable("faces", false);
                if (fd) {
                    fd->set_property_coloring(easy3d::State::FACE, "f:color");
                    fd->set_opacity(1.0f);
                    fd->set_lighting(false);
                    fd->set_distinct_back_color(false);
                    fd->update();
                }
                if (saved_current)
                    viewer->set_current_model_silent(saved_current);
                viewer->mark_dirty();

            }
        };
        // Drain whether busy or idle - the worker may push final events
        // (Done / last ShapeAccepted) after controller busy already flipped off.
        drain_live();

        // Once the run is over, hide the transient sampling/candidate overlays
        // (the persistent plane patches stay). The runner ref is also cleared
        // so the drain doesn't keep firing on stale events.
        static bool prev_busy = false;
        if (prev_busy && !busy && win) {
            win->overlays().clear_ransac_live_overlays();
        }
        prev_busy = busy;

        if (busy) {
            // Live stats: prefer reading directly from the runner (atomic
            // counters updated by the visitor each step). Falls back to
            // the dialog-state ints for non-live runs.
            auto run = s.runner;
            int shapes = run ? run.total_shapes() : s.running_shapes;
            int total_pts = run ? run.total_points() : n_pts;
            int remaining = run ? std::max(0, total_pts - 0) : s.running_remaining;
            (void)remaining;
            if (s.live_preview) {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Running... Shapes: %d | Drawn: %d / %d pts",
                    shapes, s.live_shapes_added, total_pts);
            } else {
                float ela = (float)ImGui::GetTime() - s.running_start_time;
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "Running... %s (%.1fs)",
                    win->algorithm_controller().current_label().c_str(), ela);
            }
            claw_ui::same_line_if_fits_text("|| PAUSED");
            if (run && run.is_paused()) {
                ImGui::TextColored(claw_ui::status_warning_color(), "|| PAUSED");
                claw_ui::same_line_if_fits_button("Resume");
                if (ImGui::SmallButton("Resume")) run.resume();
                claw_ui::same_line_if_fits_button("Step");
                if (ImGui::SmallButton("Step"))  run.step();
                claw_ui::same_line_if_fits_text("(1 batch)");
                ImGui::TextDisabled("(1 batch)");
            } else {
                if (ImGui::SmallButton("Pause")) { if (run) run.pause(); }
            }
            claw_ui::same_line_if_fits_button("Cancel");
            if (ImGui::SmallButton("Cancel")) {
                if (run) run.cancel();
                if (win)
                    win->algorithm_controller().request_cancel();
            }
        } else if (ImGui::Button("Detect Primitives")) {
            if (!win) { /* skip */ } else {
                bool live = s.live_preview, throttle = s.live_throttle;
                s.running_step = 0; s.running_shapes = 0;
                s.running_remaining = n_pts;
                s.running_start_time = (float)ImGui::GetTime();
                // Reset live-preview pipeline
                s.live_shapes_added = 0;
                s.runner.reset();
                if (win) win->overlays().clear_ransac_live_overlays();

                float eps = s.epsilon, nth = s.normal_threshold;
                float ceps = s.cluster_epsilon; int mp = s.min_points;
                RANSAC_Config cfg{eps, nth, ceps, mp, live, throttle};
                claw3d::services::RansacDetectionJobStart request;
                request.source_cloud = cloud;
                request.source_handle =
                    (win && win->viewer())
                        ? win->viewer()->model_handle(cloud)
                        : ModelHandle{};
                request.config = cfg;
                request.wake_ui = []() { claw3d::app::wake_event_loop(); };

                auto runner = claw3d::services::start_ransac_detection_job(
                    win->algorithm_controller(), request);
                s.runner = runner;
                if (!runner) {
                    LOG(WARNING) << "Failed to start RANSAC detection job";
                }
            }
        }
#else
        ImGui::TextColored(claw_ui::status_error_color(),
            "CGAL not available. Enable CLAW3D_ENABLE_CGAL in CMake.");
#endif
        claw_ui::same_line_if_fits_button("Close");
        if (ImGui::Button("Close")) open = false;

        // AI result evaluation button (after RANSAC completes).
        if (win && !win->algorithm_controller().is_running()
            && win->algorithm_controller().has_quality_context_for(
                AlgorithmId::RansacPrimitive)) {
            ImGui::Spacing();
            const bool has_key = win->ai_chat() && win->ai_chat()->HasApiKey();
            if (!has_key)
                ImGui::BeginDisabled();
            if (ImGui::Button("AI Evaluate Result")) {
                const std::string quality_context =
                    win->algorithm_controller().take_quality_context();
                win->send_ai_request(
                    std::string("Analyze the RANSAC primitive extraction "
                                "result above. Are the detected shapes "
                                "reasonable? Any parameter suggestions? ") +
                        ai_lang::directive(),
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    quality_context,
                    "Ask AI: evaluate RANSAC result");
            }
            if (!has_key) {
                ImGui::EndDisabled();
                claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                ImGui::TextDisabled("Set API key in AI Chat");
            }
            claw_ui::same_line_if_fits_text("(?)");
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Send the extraction report to AI for analysis.");
        }
        } // end if (can_run)
    } DIALOG_END;
}
