// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/acvd_dialog.h"
#include "dialogs/alpha_wrap_dialog.h"
#include "dialogs/arap_deformation_dialog.h"
#include "dialogs/cgal_simplification_dialog.h"
#include "dialogs/cgal_smoothing_dialog.h"
#include "dialogs/mcf_skeletonization_dialog.h"
#include "dialogs/parameterization_dialog.h"
#include "dialogs/planar_patch_remeshing_dialog.h"
#include "dialogs/ransac_dialog.h"
#include "dialogs/region_growing_dialog.h"
#include "dialogs/vsa_dialog.h"

#include "imgui.h"

namespace {

void render_cgal_unavailable_dialog(const char* title, bool& open) {
    if (!open)
        return;
    if (ImGui::Begin(title, &open)) {
        ImGui::TextWrapped(
            "This command requires 3D Claw to be built with CGAL modules.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Reconfigure with CLAW3D_ENABLE_CGAL=ON and provide the CGAL, "
            "Boost, GMP, and MPFR dependency paths described in BUILDING.md.");
    }
    ImGui::End();
}

} // namespace

void renderDialogAlphaWrapping(ViewportCanvas*, AlphaWrappingState&, bool& open) {
    render_cgal_unavailable_dialog("Alpha Wrapping 3D", open);
}

void renderDialogRansac(ViewportCanvas*, RansacState&, bool& open) {
    render_cgal_unavailable_dialog("RANSAC Primitive Extraction", open);
}

void renderDialogRegionGrowing(ViewportCanvas*, RegionGrowingState&, bool& open) {
    render_cgal_unavailable_dialog("Region Growing", open);
}

void renderDialogCGALSimplification(ViewportCanvas*, CGALSimplificationState&, bool& open) {
    render_cgal_unavailable_dialog("CGAL Surface Mesh Simplification", open);
}

void renderDialogACVD(ViewportCanvas*, ACVDState&, bool& open) {
    render_cgal_unavailable_dialog("ACVD Remeshing", open);
}

void renderDialogVSA(ViewportCanvas*, VSAState&, bool& open) {
    render_cgal_unavailable_dialog("VSA Approximation", open);
}

void renderDialogPlanarPatchRemeshing(ViewportCanvas*, PPRState&, bool& open) {
    render_cgal_unavailable_dialog("Planar Patch Remeshing", open);
}

void renderDialogCGALSmoothing(ViewportCanvas*, SmoothingState&, bool& open) {
    render_cgal_unavailable_dialog("CGAL Mesh Smoothing", open);
}

void renderDialogMCFSkeletonization(ViewportCanvas*, MCFSkelState&, bool& open) {
    render_cgal_unavailable_dialog("MCF Skeletonization", open);
}

void renderDialogARAPDeformation(ViewportCanvas*, ARAPDeformationState&, bool& open) {
    render_cgal_unavailable_dialog("ARAP Deformation", open);
}

void renderDialogParameterization(ViewportCanvas*, ParameterizationState&, bool& open) {
    render_cgal_unavailable_dialog("Parameterization", open);
}
