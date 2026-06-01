// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "ui/layout_helpers.h"
#include "ui/walk_through.h"
#include "product_identity.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

#include <string>


// =============================================================================
// Dialog rendering: each frame, render any open dialog
// =============================================================================
void MainWindow::render_dialogs() {
    if (dlg_gaussian_noise_)     renderDialogGaussianNoise(viewer(), st_gaussian_noise_, dlg_gaussian_noise_);
    if (dlg_pc_simplify_)        renderDialogPointCloudSimplify(viewer(), st_pc_simplify_, dlg_pc_simplify_);
    if (dlg_snapshot_)           renderDialogSnapshot(viewer(), st_snapshot_, dlg_snapshot_);
    if (dlg_animation_)          renderDialogAnimation(viewer(), this, st_animation_, dlg_animation_);
    else if (dlg_animation_was_open_ && walk_through_) {
        // Dialog just closed: stop preview if running, restore status so the
        // Alt+Click and K shortcuts no longer fire in viewport.
        if (walk_through_->interpolator() && walk_through_->interpolator()->is_interpolation_started())
            walk_through_->interpolator()->stop_interpolation();
        // If recording was running, abort cleanly: restore overlay visibility
        // we hijacked when recording started.
        if (st_animation_.rec_active) {
            walk_through_->set_path_visible(st_animation_.rec_path_was_visible);
            walk_through_->set_cameras_visible(st_animation_.rec_cams_was_visible);
            LOG(INFO) << "recording aborted (dialog closed): "
                      << st_animation_.rec_ok << " frames written, "
                      << st_animation_.rec_fail << " failed";
            st_animation_.rec_active = false;
            st_animation_.rec_cancel = false;
            st_animation_.rec_frames.clear();
        }
        walk_through_->set_status(WalkThrough::STOPPED);
        viewer()->mark_dirty();
    }
    dlg_animation_was_open_ = dlg_animation_;
    if (dlg_sm_curvature_)       renderDialogSurfaceMeshCurvature(viewer(), st_sm_curvature_, dlg_sm_curvature_);
    if (dlg_sm_sampling_)        renderDialogSurfaceMeshSampling(viewer(), st_sm_sampling_, dlg_sm_sampling_);
    if (dlg_sm_simplification_)  renderDialogSurfaceMeshSimplification(viewer(), st_sm_simplification_, dlg_sm_simplification_);
    if (dlg_sm_smoothing_)       renderDialogSurfaceMeshSmoothing(viewer(), st_sm_smoothing_, dlg_sm_smoothing_);
    if (dlg_sm_fairing_)         renderDialogSurfaceMeshFairing(viewer(), st_sm_fairing_, dlg_sm_fairing_);
    if (dlg_sm_hole_filling_)    renderDialogSurfaceMeshHoleFilling(viewer(), st_sm_hole_filling_, dlg_sm_hole_filling_);
    if (dlg_sm_parameterization_)renderDialogSurfaceMeshParameterization(viewer(), st_sm_parameterization_, dlg_sm_parameterization_);
    if (dlg_sm_remeshing_)       renderDialogSurfaceMeshRemeshing(viewer(), st_sm_remeshing_, dlg_sm_remeshing_);
    if (dlg_pc_normals_)         renderDialogPointCloudNormalEstimation(viewer(), st_pc_normals_, dlg_pc_normals_);
    if (dlg_poisson_)            renderDialogPoissonReconstruction(viewer(), st_poisson_, dlg_poisson_);
    if (dlg_properties_)         renderDialogProperties(viewer(), st_properties_, dlg_properties_);
    if (dlg_alpha_wrap_)         renderDialogAlphaWrapping(viewer(), st_alpha_wrap_, dlg_alpha_wrap_);
    if (dlg_ransac_)             renderDialogRansac(viewer(), st_ransac_, dlg_ransac_);
    if (dlg_region_growing_)     renderDialogRegionGrowing(viewer(), st_region_growing_, dlg_region_growing_);
    if (dlg_cgal_simpl_)         renderDialogCGALSimplification(viewer(), st_cgal_simpl_, dlg_cgal_simpl_);
    if (dlg_acvd_)               renderDialogACVD(viewer(), st_acvd_, dlg_acvd_);
    if (dlg_vsa_)                renderDialogVSA(viewer(), st_vsa_, dlg_vsa_);
    if (dlg_ppr_)                renderDialogPlanarPatchRemeshing(viewer(), st_ppr_, dlg_ppr_);
    if (dlg_cgal_smoothing_)     renderDialogCGALSmoothing(viewer(), st_cgal_smoothing_, dlg_cgal_smoothing_);
    if (dlg_geo_)                renderDialogGeodesicDistance(viewer(), st_geo_, dlg_geo_);
    if (dlg_mcf_)                renderDialogMCFSkeletonization(viewer(), st_mcf_, dlg_mcf_);
    if (dlg_arap_)               renderDialogARAPDeformation(viewer(), st_arap_, dlg_arap_);
    if (dlg_param_)              renderDialogParameterization(viewer(), st_param_, dlg_param_);
    if (dlg_3dgen_)              renderDialog3DGeneration(viewer(), st_3dgen_, dlg_3dgen_);
    {
        bool was_open = dlg_measure_;
        if (dlg_measure_) renderDialogMeasurement(viewer(), this, st_measure_, dlg_measure_);
        // Measurement dialog closed (X clicked) while picking was still on:
        // reset so subsequent viewport clicks don't get hijacked by stale
        // measurement_state_->picking==true via try_measurement_pick.
        if (was_open && !dlg_measure_ && st_measure_.picking) {
            st_measure_.picking = false;
            st_measure_.points.clear();
            if (st_measure_.history.empty())
                clear_measurement_overlay();
            else
                update_measurement_overlay(st_measure_);
            st_measure_.overlay_dirty = false;
        }
    }
    if (dlg_crop_) {
        renderDialogCrop(viewer(), this, st_crop_, dlg_crop_);
        if (!dlg_crop_) clear_crop_overlay();
    }
    // Transform tab: keep the live preview + gizmo in sync each frame
    // while the dialog is open; clean both up on close.
    {
        const bool was_open = dlg_align_;
        viewer_.set_align_state(dlg_align_ ? &st_align_ : nullptr);
        if (dlg_align_) {
            renderDialogAlign(viewer(), this, st_align_, dlg_align_);
            if (st_align_.tab == AlignTab::Transform) {
                // Apply preview FIRST so bb.center() reflects the current state,
                // THEN update gizmo to match the new position.
                apply_transform_preview(viewer_.current_model(), st_align_);
                update_align_gizmo(st_align_);
            } else {
                clear_align_gizmo();
                reset_transform_preview(viewer_.current_model(), st_align_);
                viewer_.set_selection_bbox_suppressed(false);
            }
        }
        if (was_open && !dlg_align_) {
            clear_align_gizmo();
            reset_transform_preview(viewer_.current_model(), st_align_);
            viewer_.set_selection_bbox_suppressed(false);
            viewer_.set_align_state(nullptr);
            // Restore selection bbox visibility
            if (viewer_.current_model())
                viewer_.rebuild_selection_bbox(viewer_.current_model());
        }
    }
    if (dlg_pc_mesh_dist_)       renderDialogPointCloudMeshDistance(viewer(), st_pc_mesh_dist_, dlg_pc_mesh_dist_);
    if (dlg_pc_pc_dist_)         renderDialogPointCloudPointCloudDistance(viewer(), st_pc_pc_dist_, dlg_pc_pc_dist_);
    if (dlg_about_)              renderAboutDialog();
    render_delete_selection_confirmation();
}

void MainWindow::renderAboutDialog() {
    if (!dlg_about_)
        return;
    ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About 3D Claw", &dlg_about_)) {
        ImGui::Text("%s - AI-assisted 3D Geometry Processing", product_identity::kDisplayName);
        ImGui::Text("Built on Easy3D 2.6.1");
        ImGui::Separator();
        ImGui::Text("Easy3D author: Liangliang Nan");
        ImGui::Text("Easy3D: https://3d.bk.tudelft.nl/liangliang/software/easy3d_doc/html/");
        ImGui::Separator();
        ImGui::Text("UI: Dear ImGui");
        if (ImGui::Button("OK"))
            dlg_about_ = false;
    }
    ImGui::End();
}

void MainWindow::open_named_dialog(const char* id) {
    if (!id)
        return;
    std::string s = id;
    if      (s == "simplify_mesh")        dlg_sm_simplification_ = true;
    else if (s == "cgal_simplify_mesh")   dlg_cgal_simpl_ = true;
    else if (s == "smooth_mesh")          dlg_sm_smoothing_ = true;
    else if (s == "cgal_smooth_mesh")     dlg_cgal_smoothing_ = true;
    else if (s == "fair_mesh")            dlg_sm_fairing_ = true;
    else if (s == "fill_holes")           dlg_sm_hole_filling_ = true;
    else if (s == "remesh")               dlg_sm_remeshing_ = true;
    else if (s == "sample_mesh")          dlg_sm_sampling_ = true;
    else if (s == "poisson")              dlg_poisson_ = true;
    else if (s == "alpha_wrap")           dlg_alpha_wrap_ = true;
    else if (s == "estimate_normals")     dlg_pc_normals_ = true;
    else
        LOG(WARNING) << "open_named_dialog: unknown id " << s;
}

void MainWindow::render_delete_selection_confirmation() {
    if (dlg_confirm_delete_sel_) {
        ImGui::OpenPopup("Delete Selection?");
        dlg_confirm_delete_sel_ = false;
    }
    if (!ImGui::BeginPopupModal("Delete Selection?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto* model = viewer_.current_model();
    int nf = 0;
    int nv = 0;
    int np = 0;
    if (model) {
        if (auto* m = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
            nf = selection_manager_.selected_count(
                m, SelectionElementType::SurfaceFace);
            nv = selection_manager_.selected_count(
                m, SelectionElementType::SurfaceVertex);
        } else if (auto* c = dynamic_cast<easy3d::PointCloud*>(model)) {
            np = selection_manager_.selected_count(
                c, SelectionElementType::PointCloudPoint);
        }
    }

    const char* mname = model ? model->name().c_str() : "(none)";
    if (nf > 0)
        ImGui::Text("Delete %d face(s) from \"%s\"?", nf, mname);
    else if (nv > 0)
        ImGui::Text("Delete %d vertex/vertices from \"%s\"?", nv, mname);
    else if (np > 0)
        ImGui::Text("Delete %d point(s) from \"%s\"?", np, mname);
    else
        ImGui::Text("No selection to delete.");

    ImGui::TextColored(claw_ui::status_warning_color(),
                       "This modifies the model in place and cannot be undone.");
    ImGui::Separator();
    if (ImGui::Button("Yes, delete", ImVec2(120, 0))) {
        delete_selection();
        ImGui::CloseCurrentPopup();
    }
    claw_ui::same_line_if_fits_width(120.0f);
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
