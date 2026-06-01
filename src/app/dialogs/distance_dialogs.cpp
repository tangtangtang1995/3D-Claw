// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "services/operations/easy3d_model_operations.h"
#include "ui/layout_helpers.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <cfloat>
#include <cstddef>
#include <vector>

namespace {

bool valid_dialog_index(int index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

} // namespace

void renderDialogPointCloudMeshDistance(ViewportCanvas* viewer, PointCloudMeshDistState& s, bool& open) {
    prepare_dialog_window(520, 480);
    DIALOG_BODY("Point Cloud <-> Mesh Distance", open) {
        const auto& all = viewer->models();
        std::vector<easy3d::PointCloud*> clouds;
        std::vector<easy3d::SurfaceMesh*> meshes;
        for (auto& sp : all) {
            auto* m = sp.get();
            if (auto* pc = dynamic_cast<easy3d::PointCloud*>(m)) clouds.push_back(pc);
            else if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(m)) meshes.push_back(sm);
        }

        if (s.computed &&
            (!valid_dialog_index(s.source_idx, clouds.size()) ||
             !valid_dialog_index(s.target_idx, meshes.size()))) {
            s.computed = false;
            s.source_idx = -1;
            s.target_idx = -1;
        }

        if (clouds.empty() || meshes.empty()) {
            ImGui::TextWrapped("This tool computes the nearest-point distance from each point "
                               "in a source point cloud to the surface of a target mesh.");
            ImGui::Spacing();
            ImGui::TextColored(claw_ui::status_warning_color(), "Required:");
            ImGui::Text("- At least 1 PointCloud (source)");
            ImGui::Text("- At least 1 SurfaceMesh (target)");
            ImGui::Spacing();
            ImGui::Text("Currently loaded models:");
            ImGui::BulletText("PointClouds: %d", (int)clouds.size());
            ImGui::BulletText("SurfaceMeshes: %d", (int)meshes.size());
            ImGui::Spacing();
            if (clouds.empty())
                ImGui::TextColored(claw_ui::status_error_color(), "-> Please load a point cloud and a mesh file.");
            else
                ImGui::TextColored(claw_ui::status_error_color(), "-> Please load a mesh file (target).");
        }
        else if (s.computed) {
            auto* pc = clouds[s.source_idx];
            ImGui::Text("Source: %s (%d pts)", pc->name().c_str(), pc->n_vertices());
            ImGui::Text("Target: %s (%dv, %df)", meshes[s.target_idx]->name().c_str(),
                meshes[s.target_idx]->n_vertices(), meshes[s.target_idx]->n_faces());
            ImGui::Separator();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_w = ImGui::GetContentRegionAvail().x - 20;
            float bar_h = 18;
            for (int i = 0; i < (int)bar_w; i++)
                dl->AddRectFilled(ImVec2(pos.x + i, pos.y), ImVec2(pos.x + i + 1, pos.y + bar_h),
                                  colormap_color((float)i / bar_w));
            ImGui::Dummy(ImVec2(bar_w, bar_h + 2));
            ImGui::TextDisabled("0");
            claw_ui::same_line_right_if_fits_text("0000.0000");
            ImGui::TextDisabled("%.4f", s.max_dist);
            ImGui::Spacing();

            auto dist_attr = pc->get_vertex_property<float>("v:dist");
            if (dist_attr) {
                int n = pc->n_vertices();
                float dmin = s.max_dist, dmax = s.max_dist;
                dmin = 0;
                int nbins = 50;
                std::vector<float> bins(nbins, 0);
                float range = (dmax - dmin > 0) ? (dmax - dmin) : 1.0f;
                for (auto v : pc->vertices()) {
                    float val = dist_attr[v];
                    int bi = (int)((val - dmin) / range * (nbins - 1));
                    if (bi >= 0 && bi < nbins) bins[bi] += 1.0f;
                }
                ImGui::PlotHistogram("##hist", bins.data(), nbins, 0, nullptr,
                    0, FLT_MAX, ImVec2(bar_w, 60));
            }
            ImGui::Spacing();

            auto drawable = pc->renderer()->get_points_drawable("vertices");
            float dmin = 0, dmax = s.max_dist;
            if (dmax < 0.001f) dmax = 0.001f;
            if (ImGui::SliderFloat("Min Display", &s.disp_min, dmin, dmax, "%.4f")) {
                float pct = (s.disp_min - dmin) / (dmax - dmin);
                if (pct < 0) pct = 0; if (pct > 1) pct = 1;
                drawable->set_clamp_lower(pct);
                drawable->update();
                viewer->mark_dirty();
            }
            if (ImGui::SliderFloat("Max Display", &s.disp_max, dmin, dmax, "%.4f")) {
                float pct = 1.0f - (s.disp_max - dmin) / (dmax - dmin);
                if (pct < 0) pct = 0; if (pct > 1) pct = 1;
                drawable->set_clamp_upper(pct);
                drawable->update();
                viewer->mark_dirty();
            }
            if (ImGui::Button("Reset Range")) {
                s.disp_min = dmin; s.disp_max = dmax;
                drawable->set_clamp_lower(0.02f);
                drawable->set_clamp_upper(0.02f);
                drawable->update();
                viewer->mark_dirty();
            }
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::Text("| Metric | Value |");
            ImGui::Text("|--------|-------|");
            ImGui::Text("| Max Distance | %.4f |", s.max_dist);
            ImGui::Text("| Mean Distance (Chamfer) | %.4f |", s.mean_dist);
            ImGui::Text("| RMS Distance | %.4f |", s.rms_dist);
            ImGui::Text("| StdDev | %.4f |", s.stddev_dist);
            ImGui::Spacing();
            if (ImGui::Button("Compute Again")) { s.computed = false; s.source_idx = s.target_idx = -1; }
            claw_ui::same_line_if_fits_button("Close");
            if (ImGui::Button("Close")) open = false;
        }
        else {
            if (s.source_idx < 0 || s.source_idx >= (int)clouds.size()) s.source_idx = 0;
            if (s.target_idx < 0 || s.target_idx >= (int)meshes.size()) s.target_idx = 0;
            ImGui::Text("Source Point Cloud:");
            if (ImGui::BeginCombo("##pc_src", clouds[s.source_idx]->name().c_str())) {
                for (int i = 0; i < (int)clouds.size(); i++) {
                    bool sel = (i == s.source_idx);
                    if (ImGui::Selectable(clouds[i]->name().c_str(), &sel)) s.source_idx = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Text("Target Mesh:");
            if (ImGui::BeginCombo("##mesh_tgt", meshes[s.target_idx]->name().c_str())) {
                for (int i = 0; i < (int)meshes.size(); i++) {
                    bool sel = (i == s.target_idx);
                    if (ImGui::Selectable(meshes[i]->name().c_str(), &sel)) s.target_idx = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Compute")) {
                auto* pc = clouds[s.source_idx];
                auto* mesh = meshes[s.target_idx];
                claw3d::services::DistanceStats stats;
                if (claw3d::services::compute_point_cloud_mesh_distance(pc, mesh, stats)) {
                    s.max_dist = stats.max;
                    s.mean_dist = stats.mean;
                    s.rms_dist = stats.rms;
                    s.stddev_dist = stats.stddev;
                    s.disp_min = 0;
                    s.disp_max = stats.max;
                    pc->renderer()->update();
                    auto drawable = pc->renderer()->get_points_drawable("vertices");
                    drawable->set_scalar_coloring(
                        easy3d::State::VERTEX, "v:dist", nullptr, 0.02f, 0.02f);
                    s.computed = true;
                }
            }
            claw_ui::same_line_if_fits_button("Cancel");
            if (ImGui::Button("Cancel")) open = false;
        }
    } DIALOG_END;
}

void renderDialogPointCloudPointCloudDistance(ViewportCanvas* viewer, PointCloudPointCloudDistState& s, bool& open) {
    prepare_dialog_window(520, 480);
    DIALOG_BODY("Point Cloud <-> Point Cloud Distance", open) {
        const auto& all = viewer->models();
        std::vector<easy3d::PointCloud*> clouds;
        for (auto& sp : all) {
            if (auto* pc = dynamic_cast<easy3d::PointCloud*>(sp.get())) clouds.push_back(pc);
        }

        if (s.computed &&
            (!valid_dialog_index(s.source_idx, clouds.size()) ||
             !valid_dialog_index(s.target_idx, clouds.size()))) {
            s.computed = false;
            s.source_idx = -1;
            s.target_idx = -1;
        }

        if (clouds.size() < 2) {
            ImGui::TextWrapped("This tool computes the nearest-point distance from each point "
                               "in a source cloud to its closest neighbor in a target cloud.");
            ImGui::Spacing();
            ImGui::TextColored(claw_ui::status_warning_color(), "Required: at least 2 PointClouds");
            ImGui::Spacing();
            ImGui::Text("Currently loaded PointClouds: %d", (int)clouds.size());
            if (clouds.size() < 2)
                ImGui::TextColored(claw_ui::status_error_color(), "-> Please load at least one more point cloud.");
        }
        else if (s.computed) {
            auto* src = clouds[s.source_idx];
            ImGui::Text("Source: %s (%d pts)", src->name().c_str(), src->n_vertices());
            ImGui::Text("Target: %s (%d pts)", clouds[s.target_idx]->name().c_str(), clouds[s.target_idx]->n_vertices());
            ImGui::Separator();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_w = ImGui::GetContentRegionAvail().x - 20;
            float bar_h = 18;
            for (int i = 0; i < (int)bar_w; i++)
                dl->AddRectFilled(ImVec2(pos.x + i, pos.y), ImVec2(pos.x + i + 1, pos.y + bar_h),
                                  colormap_color((float)i / bar_w));
            ImGui::Dummy(ImVec2(bar_w, bar_h + 2));
            ImGui::TextDisabled("0");
            claw_ui::same_line_right_if_fits_text("0000.0000");
            ImGui::TextDisabled("%.4f", s.max_dist);
            ImGui::Spacing();

            auto dist_attr = src->get_vertex_property<float>("v:dist");
            if (dist_attr) {
                float dmax = s.max_dist, dmin = 0.0f;
                int nbins = 50;
                std::vector<float> bins(nbins, 0);
                float range = (dmax - dmin > 0) ? (dmax - dmin) : 1.0f;
                for (auto v : src->vertices()) {
                    int bi = (int)((dist_attr[v] - dmin) / range * (nbins - 1));
                    if (bi >= 0 && bi < nbins) bins[bi] += 1.0f;
                }
                ImGui::PlotHistogram("##hist_pc", bins.data(), nbins, 0, nullptr,
                    0, FLT_MAX, ImVec2(bar_w, 60));
            }
            ImGui::Spacing();

            auto drawable = src->renderer()->get_points_drawable("vertices");
            float dmin = 0, dmax = s.max_dist;
            if (dmax < 0.001f) dmax = 0.001f;
            if (ImGui::SliderFloat("Min Display", &s.disp_min, dmin, dmax, "%.4f")) {
                float pct = (s.disp_min - dmin) / (dmax - dmin);
                if (pct < 0) pct = 0; if (pct > 1) pct = 1;
                drawable->set_clamp_lower(pct);
                drawable->update();
                viewer->mark_dirty();
            }
            if (ImGui::SliderFloat("Max Display", &s.disp_max, dmin, dmax, "%.4f")) {
                float pct = 1.0f - (s.disp_max - dmin) / (dmax - dmin);
                if (pct < 0) pct = 0; if (pct > 1) pct = 1;
                drawable->set_clamp_upper(pct);
                drawable->update();
                viewer->mark_dirty();
            }
            if (ImGui::Button("Reset Range")) {
                s.disp_min = dmin; s.disp_max = dmax;
                drawable->set_clamp_lower(0.02f);
                drawable->set_clamp_upper(0.02f);
                drawable->update();
                viewer->mark_dirty();
            }
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::Text("| Metric | Value |");
            ImGui::Text("|--------|-------|");
            ImGui::Text("| Max Distance | %.4f |", s.max_dist);
            ImGui::Text("| Mean Distance | %.4f |", s.mean_dist);
            ImGui::Text("| RMS Distance | %.4f |", s.rms_dist);
            ImGui::Text("| StdDev | %.4f |", s.stddev_dist);
            ImGui::Spacing();
            if (ImGui::Button("Compute Again")) { s.computed = false; s.source_idx = s.target_idx = -1; }
            claw_ui::same_line_if_fits_button("Close");
            if (ImGui::Button("Close")) open = false;
        }
        else {
            if (!valid_dialog_index(s.source_idx, clouds.size()) ||
                !valid_dialog_index(s.target_idx, clouds.size()) ||
                s.source_idx == s.target_idx) {
                s.source_idx = 0;
                s.target_idx = (clouds.size() > 1) ? 1 : 0;
            }
            ImGui::Text("Source Cloud:");
            if (ImGui::BeginCombo("##pc_src2", clouds[s.source_idx]->name().c_str())) {
                for (int i = 0; i < (int)clouds.size(); i++) {
                    if (i == s.target_idx) continue;
                    bool sel = (i == s.source_idx);
                    if (ImGui::Selectable(clouds[i]->name().c_str(), &sel)) s.source_idx = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Text("Target Cloud:");
            if (ImGui::BeginCombo("##pc_tgt2", clouds[s.target_idx]->name().c_str())) {
                for (int i = 0; i < (int)clouds.size(); i++) {
                    if (i == s.source_idx) continue;
                    bool sel = (i == s.target_idx);
                    if (ImGui::Selectable(clouds[i]->name().c_str(), &sel)) s.target_idx = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Compute")) {
                auto* src = clouds[s.source_idx];
                auto* tgt = clouds[s.target_idx];
                claw3d::services::DistanceStats stats;
                if (claw3d::services::compute_point_cloud_distance(src, tgt, stats)) {
                    s.max_dist = stats.max;
                    s.mean_dist = stats.mean;
                    s.rms_dist = stats.rms;
                    s.stddev_dist = stats.stddev;
                    s.disp_min = 0;
                    s.disp_max = stats.max;
                    src->renderer()->update();
                    auto drawable = src->renderer()->get_points_drawable("vertices");
                    drawable->set_scalar_coloring(
                        easy3d::State::VERTEX, "v:dist", nullptr, 0.02f, 0.02f);
                    s.computed = true;
                }
            }
            claw_ui::same_line_if_fits_button("Cancel");
            if (ImGui::Button("Cancel")) open = false;
        }
    } DIALOG_END;
}
