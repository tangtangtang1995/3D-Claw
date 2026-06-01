// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/align_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/poly_mesh.h>
#include <easy3d/core/matrix.h>
#include <easy3d/core/types.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/manipulator.h>
#include <easy3d/renderer/manipulated_frame.h>
#include <easy3d/renderer/transform.h>
#include <easy3d/util/logging.h>

#include "icp.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "selection/selection_manager.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "model/model_health.h"
#include "ui/layout_helpers.h"


namespace {

using claw3d::algo::kabsch_rigid;
using claw3d::algo::run_icp;

// -- Pull selected points out of a model in the order SelectionManager gives.
// Both source and target are expected to be the SAME size when used by the
// point-pair aligner. Picks honor the element kind:
//   SurfaceMesh -> selected vertices (preferred), else nothing
//   PointCloud  -> selected points
// Returns the world-space positions of the picks.
std::vector<easy3d::vec3>
gather_selected_points(easy3d::Model* m, SelectionManager& mgr)
{
    std::vector<easy3d::vec3> out;
    if (!m) return out;
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) {
        auto* sel = mgr.get(sm);
        if (!sel) return out;
        auto pts = sm->get_vertex_property<easy3d::vec3>("v:point");
        for (auto v : sm->vertices())
            if (sel->surface_vertices[v.idx()])
                out.push_back(pts[v]);
    } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) {
        auto* sel = mgr.get(pc);
        if (!sel) return out;
        auto pts = pc->get_vertex_property<easy3d::vec3>("v:point");
        for (auto v : pc->vertices())
            if (sel->pointcloud_points[v.idx()])
                out.push_back(pts[v]);
    }
    return out;
}


// -- Walk a model's geometry and collect its points (for ICP). For mesh we
// use vertex positions; for cloud just the points.
std::vector<easy3d::vec3> all_points(easy3d::Model* m) {
    std::vector<easy3d::vec3> out;
    if (!m) return out;
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) {
        auto pts = sm->get_vertex_property<easy3d::vec3>("v:point");
        out.reserve(sm->n_vertices());
        for (auto v : sm->vertices()) out.push_back(pts[v]);
    } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) {
        auto pts = pc->get_vertex_property<easy3d::vec3>("v:point");
        out.reserve(pc->n_vertices());
        for (auto v : pc->vertices()) out.push_back(pts[v]);
    }
    return out;
}


template <typename ModelT>
void write_baked_transform_properties(ModelT* m, const easy3d::mat4& T,
                                      const std::string& operation) {
    auto last = m->template model_property<easy3d::mat4>(
        "m:transform_last_baked", easy3d::mat4::identity());
    last[0] = T;

    auto cumulative = m->template model_property<easy3d::mat4>(
        "m:transform_cumulative_baked", easy3d::mat4::identity());
    cumulative[0] = T * cumulative[0];

    auto op = m->template model_property<std::string>(
        "m:transform_last_operation", std::string());
    op[0] = operation;

    auto count = m->template model_property<int>(
        "m:transform_baked_count", 0);
    count[0] = count[0] + 1;
}

void write_baked_transform_properties(easy3d::Model* m,
                                      const easy3d::mat4& T,
                                      const std::string& operation) {
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(m))
        write_baked_transform_properties(mesh, T, operation);
    else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(m))
        write_baked_transform_properties(cloud, T, operation);
    else if (auto* graph = dynamic_cast<easy3d::Graph*>(m))
        write_baked_transform_properties(graph, T, operation);
    else if (auto* poly = dynamic_cast<easy3d::PolyMesh*>(m))
        write_baked_transform_properties(poly, T, operation);
}

// -- Bake an mat4 transform directly into a model's vertex positions, then
// reset the manipulator. We don't use the manipulator for preview since it
// doesn't support non-uniform scale and Apply makes preview pointless.
void apply_transform_to_geometry(easy3d::Model* m, const easy3d::mat4& T,
                                 const std::string& operation = "Transform") {
    if (!m) return;
    auto& pts = m->points();
    for (auto& p : pts) p = T * p;
    m->invalidate_bounding_box();
    if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m))
        sm->update_vertex_normals();
    else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) {
        auto normal = pc->get_vertex_property<easy3d::vec3>("v:normal");
        if (normal) {
            easy3d::mat3 N = easy3d::transform::normal_matrix(T);
            for (auto v : pc->vertices()) normal[v] = N * normal[v];
        }
    }
    if (m->manipulator()) m->manipulator()->reset();
    write_baked_transform_properties(m, T, operation);
    ModelHealthRegistry::instance().invalidate(m);
    m->renderer()->update();
}


easy3d::mat4 build_TRS(float tx, float ty, float tz,
                       float rx_deg, float ry_deg, float rz_deg,
                       float sx, float sy, float sz)
{
    const float d2r = 3.14159265358979323846f / 180.0f;
    const float rx = rx_deg * d2r, ry = ry_deg * d2r, rz = rz_deg * d2r;
    const float cx = std::cos(rx), sxr = std::sin(rx);
    const float cy = std::cos(ry), syr = std::sin(ry);
    const float cz = std::cos(rz), szr = std::sin(rz);

    // R = Rz * Ry * Rx (matches the convention used by the Crop dialog).
    easy3d::mat3 R(
        cz*cy,                       cz*syr*sxr - szr*cx,   cz*syr*cx + szr*sxr,
        szr*cy,                      szr*syr*sxr + cz*cx,   szr*syr*cx - cz*sxr,
        -syr,                        cy*sxr,                 cy*cx
    );
    // Scale via column scaling: R_cols * diag(s)
    R(0,0) *= sx; R(1,0) *= sx; R(2,0) *= sx;
    R(0,1) *= sy; R(1,1) *= sy; R(2,1) *= sy;
    R(0,2) *= sz; R(1,2) *= sz; R(2,2) *= sz;

    easy3d::mat4 T(easy3d::mat4::identity());
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            T(i, j) = R(i, j);
    T(0, 3) = tx; T(1, 3) = ty; T(2, 3) = tz;
    return T;
}


// -- Combo helper: list all models with an "Auto (current)" first entry.
easy3d::Model* model_picker(const char* label, ViewportCanvas* viewer,
                            easy3d::Model* current)
{
    const auto& models = viewer->models();
    std::vector<easy3d::Model*> options;
    options.reserve(models.size() + 1);
    options.push_back(nullptr); // "Auto (current)"
    for (auto& mp : models) options.push_back(mp.get());

    int sel_idx = 0;
    for (std::size_t i = 1; i < options.size(); ++i)
        if (options[i] == current) { sel_idx = (int)i; break; }

    if (ImGui::BeginCombo(label, sel_idx == 0 ? "(Auto: current model)"
                                              : options[sel_idx]->name().c_str())) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            bool is_sel = ((int)i == sel_idx);
            const char* name = (i == 0) ? "(Auto: current model)"
                                        : options[i]->name().c_str();
            if (ImGui::Selectable(name, is_sel)) current = options[i];
            if (is_sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return current;
}


easy3d::Model* resolve_or_current(easy3d::Model* explicit_choice,
                                  ViewportCanvas* viewer)
{
    return explicit_choice ? explicit_choice : (viewer ? viewer->current_model() : nullptr);
}

} // namespace


void renderDialogAlign(ViewportCanvas* viewer, MainWindow* win,
                       AlignState& s, bool& open)
{
    prepare_dialog_window(560, 620);
    DIALOG_BODY("Transform / Align", open) {
        prereq_hint_only(prereq_any_model(viewer));
        if (ImGui::BeginTabBar("AlignTabs", ImGuiTabBarFlags_None)) {

            // ============ Transform ============
            if (ImGui::BeginTabItem("Transform")) {
                s.tab = AlignTab::Transform;
                auto* target = viewer ? viewer->current_model() : nullptr;
                if (!target) {
                    ImGui::TextDisabled("No model selected.");
                } else {
                    ImGui::TextWrapped("Target: %s", target->name().c_str());
                    ImGui::Separator();
                    ImGui::Text("Translation");
                    ImGui::DragFloat3("T (x,y,z)", &s.tx, 0.01f);
                    ImGui::Text("Rotation (Euler degrees, Rz*Ry*Rx)");
                    ImGui::DragFloat3("R (x,y,z)", &s.rx, 0.5f);
                    ImGui::Text("Scale");
                    if (ImGui::Checkbox("Uniform", &s.uniform_scale) && s.uniform_scale) {
                        s.sy = s.sx; s.sz = s.sx;
                    }
                    if (s.uniform_scale) {
                        if (ImGui::DragFloat("S", &s.sx, 0.01f, 0.001f, 1000.0f)) {
                            s.sy = s.sx; s.sz = s.sx;
                        }
                    } else {
                        ImGui::DragFloat3("S (x,y,z)", &s.sx, 0.01f, 0.001f, 1000.0f);
                    }
                    ImGui::Separator();
                    if (ImGui::Button("Reset")) {
                        if (s.preview_model)
                            win->reset_transform_preview(target, s);
                        s.tx = s.ty = s.tz = 0.f;
                        s.rx = s.ry = s.rz = 0.f;
                        s.sx = s.sy = s.sz = 1.f;
                        s.transform_status.clear();
                        if (viewer) {
                            viewer->set_selection_bbox_suppressed(false);
                            viewer->mark_dirty();
                            viewer->rebuild_selection_bbox(target);
                        }
                    }
                    claw_ui::same_line_if_fits_button("Apply to Geometry");
                    if (ImGui::Button("Apply to Geometry")) {
                        auto T = build_TRS(s.tx, s.ty, s.tz, s.rx, s.ry, s.rz,
                                           s.sx, s.sy, s.sz);
                        // Clear preview cache before baking; geometry is about to
                        // be permanently modified
                        if (s.preview_model)
                            win->reset_transform_preview(target, s);
                        apply_transform_to_geometry(target, T, "Transform / manual TRS");
                        char buf[80];
                        std::snprintf(buf, sizeof(buf),
                            "Applied: T(%.3f,%.3f,%.3f) R(%.1f,%.1f,%.1f) S(%.3f,%.3f,%.3f)",
                            s.tx, s.ty, s.tz, s.rx, s.ry, s.rz, s.sx, s.sy, s.sz);
                        s.transform_status = buf;
                        s.preview_model = nullptr;
                        s.preview_original_pts.clear();
                        s.tx = s.ty = s.tz = 0.f;
                        s.rx = s.ry = s.rz = 0.f;
                        s.sx = s.sy = s.sz = 1.f;
                        if (viewer) {
                            viewer->set_selection_bbox_suppressed(false);
                            viewer->mark_dirty();
                            viewer->rebuild_selection_bbox(target);
                        }
                    }
                    claw_ui::same_line_if_fits_text("Show 3D Gizmo");
                    ImGui::Checkbox("Show 3D Gizmo", &s.transform_show_gizmo);
                }
                if (!s.transform_status.empty())
                    ImGui::TextColored(claw_ui::status_success_color(), "%s",
                                       s.transform_status.c_str());
                ImGui::EndTabItem();
            }

            // ============ Point-pair alignment ============
            if (ImGui::BeginTabItem("Point-Pair")) {
                s.tab = AlignTab::PointPair;
                ImGui::TextWrapped("Pick N corresponding vertices/points on the two models via Select menu, "
                                   "then choose them here and click Apply. N must be >= 3, and the picks "
                                   "must be in matching order (1st source picked = 1st target picked).");
                ImGui::Separator();
                s.pp_source_model = model_picker("Source", viewer, s.pp_source_model);
                s.pp_target_model = model_picker("Target", viewer, s.pp_target_model);

                auto* src = s.pp_source_model;
                auto* dst = s.pp_target_model;
                if (!src || !dst || src == dst) {
                    ImGui::TextDisabled("Pick two different models above (Source != Target).");
                } else if (win) {
                    auto src_pts = gather_selected_points(src, win->selection_manager());
                    auto dst_pts = gather_selected_points(dst, win->selection_manager());
                    ImGui::Text("Source picks: %zu", src_pts.size());
                    ImGui::Text("Target picks: %zu", dst_pts.size());
                    bool ready = src_pts.size() >= 3 && src_pts.size() == dst_pts.size();
                    if (!ready)
                        ImGui::TextColored(claw_ui::status_warning_color(),
                            "Need >=3 picks on EACH model AND equal counts.");
                    if (ImGui::Button("Compute & Apply (moves Source onto Target)")) {
                        easy3d::mat4 T;
                        float rms = 0.f;
                        if (kabsch_rigid(src_pts, dst_pts, T, rms)) {
                            apply_transform_to_geometry(src, T, "Point-Pair alignment");
                            s.pp_last_rms = rms;
                            s.pp_last_pair_count = (int)src_pts.size();
                            char buf[120];
                            std::snprintf(buf, sizeof(buf),
                                "Aligned via %zu pairs. Post-fit RMS = %.6f.",
                                src_pts.size(), rms);
                            s.pp_status = buf;
                            if (viewer) {
                                viewer->mark_dirty();
                                if (viewer->current_model() == src)
                                    viewer->rebuild_selection_bbox(src);
                            }
                        } else {
                            s.pp_status = "Kabsch failed (need >=3 matched pairs).";
                            s.pp_last_rms = -1.f;
                        }
                    }
                    if (!s.pp_status.empty())
                        ImGui::TextColored(claw_ui::status_success_color(), "%s",
                                           s.pp_status.c_str());
                }
                ImGui::EndTabItem();
            }

            // ============ ICP ============
            if (ImGui::BeginTabItem("ICP")) {
                s.tab = AlignTab::ICP;
                ImGui::TextWrapped("Point-to-point ICP. Iteratively transforms Source so its nearest-neighbor "
                                   "distances to Target are minimized. Source and Target may be mesh or cloud; "
                                   "only their vertex positions are used. Works best when the two are already "
                                   "roughly aligned (use Point-Pair first when needed).");
                ImGui::Separator();
                s.icp_source_model = model_picker("Source", viewer, s.icp_source_model);
                s.icp_target_model = model_picker("Target", viewer, s.icp_target_model);
                ImGui::DragInt("Max iterations", &s.icp_max_iter, 1.f, 1, 500);
                ImGui::DragInt("Source sample count", &s.icp_sample_count, 10.f, 100, 200000);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 or larger than the source = no subsampling (slower).");
                ImGui::DragFloat("Outlier factor (k * median)", &s.icp_outlier_factor,
                                 0.05f, 1.0f, 20.0f, "%.2f");
                ImGui::DragFloat("RMS convergence tolerance", &s.icp_tolerance,
                                 1e-6f, 1e-8f, 1e-2f, "%.7f");

                auto* src = s.icp_source_model;
                auto* dst = s.icp_target_model;
                if (!src || !dst || src == dst) {
                    ImGui::TextDisabled("Pick two different models above (Source != Target).");
                } else {
                    if (ImGui::Button("Run ICP")) {
                        auto src_pts = all_points(src);
                        auto dst_pts = all_points(dst);
                        auto r = run_icp(src_pts, dst_pts,
                                         s.icp_max_iter, s.icp_sample_count,
                                         s.icp_outlier_factor, s.icp_tolerance);
                        if (r.ok) {
                            apply_transform_to_geometry(src, r.T, "ICP alignment");
                            s.icp_last_rms = r.rms;
                            s.icp_last_iter = r.iterations_run;
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "Converged in %d iter. RMS = %.6f over %d correspondences.",
                                r.iterations_run, r.rms, r.correspondences);
                            s.icp_status = buf;
                            if (viewer) {
                                viewer->mark_dirty();
                                if (viewer->current_model() == src)
                                    viewer->rebuild_selection_bbox(src);
                            }
                        } else {
                            s.icp_status = "ICP failed (too few correspondences or numerical issue).";
                            s.icp_last_rms = -1.f;
                        }
                    }
                }
                if (!s.icp_status.empty())
                    ImGui::TextColored(claw_ui::status_success_color(), "%s",
                                       s.icp_status.c_str());
                ImGui::Spacing();
                if (ImGui::Button("Ask AI About Result")) {
                    char ctx[256];
                    std::snprintf(ctx, sizeof(ctx),
                        "Last ICP run: iterations=%d, final RMS=%.6f, "
                        "max_iter=%d, sample_count=%d, outlier_factor=%.2f.",
                        s.icp_last_iter, s.icp_last_rms,
                        s.icp_max_iter, s.icp_sample_count, s.icp_outlier_factor);
                    if (win) {
                        win->send_ai_request(
                            std::string(
                                "Given [Current Model] and the ICP run summary in the "
                                "extra context, judge whether the alignment likely "
                                "succeeded, and if RMS is high suggest what might have "
                                "gone wrong (initial pose, scale mismatch, low overlap, "
                                "wrong source/target choice). ") + ai_lang::directive(),
                            AICtx_CurrentModel | AICtx_Scene | AICtx_Runtime, ctx,
                            "Ask AI: evaluate ICP alignment result");
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    } DIALOG_END;
}
