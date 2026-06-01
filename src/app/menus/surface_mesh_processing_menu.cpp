// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "services/operations/easy3d_model_operations.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include "imgui.h"

void MainWindow::render_menu_surface_mesh_processing() {
    if (ImGui::BeginMenu("Subdivision")) {
        if (ImGui::MenuItem("Catmull-Clark")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
            if (claw3d::services::apply_surface_mesh_subdivision(
                    mesh, claw3d::services::SubdivisionScheme::CatmullClark)) {
                mesh->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "Catmull-Clark subdivision done";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quad-based subdivision. Each quad face produces 4 sub-quads.\n"
                              "Generates smooth limit surfaces. Best for quad-dominant meshes.\n"
                              "On triangle meshes, splits each triangle into 3 quads.");
        if (ImGui::MenuItem("Loop")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
            if (claw3d::services::apply_surface_mesh_subdivision(
                    mesh, claw3d::services::SubdivisionScheme::Loop)) {
                mesh->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "Loop subdivision done";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Triangle-based subdivision. Each triangle produces 4 sub-triangles.\n"
                              "C2-continuous limit surface except at irregular vertices.\n"
                              "Best for pure triangle meshes.");
        if (ImGui::MenuItem("Sqrt3")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
            if (claw3d::services::apply_surface_mesh_subdivision(
                    mesh, claw3d::services::SubdivisionScheme::Sqrt3)) {
                mesh->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "Sqrt3 subdivision done";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Triangle subdivision by inserting vertex at face centroid + edge flipping.\n"
                              "Slower refinement rate (~3x per step) but produces more uniform triangles\n"
                              "than Loop subdivision.");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Simplify")) {
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("CGAL Quality (LT / GH)... [recommended]"))
            open_dialog(dlg_cgal_simpl_, st_cgal_simpl_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Edge-collapse decimation with Lindstrom-Turk / Garland-Heckbert\n"
                              "quality metrics. Best balance of size vs detail preservation.");
#else
        menu_cgal_required_item(
            "CGAL Quality (LT / GH)...",
            "Edge-collapse decimation with Lindstrom-Turk / Garland-Heckbert quality metrics.");
#endif
        if (ImGui::MenuItem("Easy3D Fast Decimate..."))
            open_dialog(dlg_sm_simplification_, st_sm_simplification_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Easy3D built-in edge-collapse decimator. Faster but less\n"
                              "feature-preserving than CGAL.");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Smooth / Fair")) {
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("CGAL Angle / Area / MCF... [recommended]")) {
            if (algorithm_controller().is_running_id(AlgorithmId::CgalSmoothing))
                dlg_cgal_smoothing_ = true;
            else
                open_dialog(dlg_cgal_smoothing_, st_cgal_smoothing_);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tangential / Angle / Mean Curvature Flow smoothing (CGAL PMP).\n"
                              "Quality-preserving, configurable, the default choice.");
#else
        menu_cgal_required_item(
            "CGAL Angle / Area / MCF...",
            "Tangential, angle-based, and mean-curvature-flow smoothing.");
#endif
        if (ImGui::MenuItem("Easy3D Laplacian..."))
            open_dialog(dlg_sm_smoothing_, st_sm_smoothing_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Plain Laplacian: average each vertex with its neighbors.\n"
                              "Fast but causes volume shrinkage.");
        if (ImGui::MenuItem("Fairing..."))
            open_dialog(dlg_sm_fairing_, st_sm_fairing_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Curvature-minimizing fairing. Smoother than Laplacian,\n"
                              "better at preserving overall shape.");
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Hole Filling..."))
        open_dialog(dlg_sm_hole_filling_, st_sm_hole_filling_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fill boundary holes by triangulation.");

    if (ImGui::BeginMenu("Remesh")) {
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("ACVD... [recommended]")) {
            if (algorithm_controller().is_running_id(AlgorithmId::AcvdRemeshing))
                dlg_acvd_ = true;
            else
                open_dialog(dlg_acvd_, st_acvd_);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Centroidal-Voronoi isotropic remesher with explicit target\n"
                              "vertex count. Uniform triangles, FE-quality output.");
        if (ImGui::MenuItem("Easy3D Isotropic..."))
            open_dialog(dlg_sm_remeshing_, st_sm_remeshing_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Easy3D built-in uniform / adaptive remesher.");
        if (ImGui::MenuItem("VSA Approximation (CGAL)...")) {
            if (algorithm_controller().is_running_id(AlgorithmId::VsaApproximation))
                dlg_vsa_ = true;
            else
                open_dialog(dlg_vsa_, st_vsa_);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Variational shape approximation: segment mesh into proxy planes.\n"
                              "For CAD / building-like geometry.");
        if (ImGui::MenuItem("Planar Patch Remeshing (CGAL)..."))
            open_dialog(dlg_ppr_, st_ppr_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Detect coplanar patches and remesh them with sharp edges + corners.");
#else
        menu_cgal_required_item(
            "ACVD...",
            "Centroidal-Voronoi isotropic remeshing with explicit target vertex count.");
        menu_cgal_required_item(
            "VSA Approximation...",
            "Variational shape approximation into proxy planes.");
        menu_cgal_required_item(
            "Planar Patch Remeshing...",
            "Detect coplanar patches and remesh them with sharp edges and corners.");
#endif
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Parameterize / UV")) {
#ifdef CLAW3D_HAS_CGAL
        if (ImGui::MenuItem("CGAL LSCM (UV view)... [recommended]"))
            open_dialog(dlg_param_, st_param_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("LSCM angle-preserving UV parameterization with side-by-side\n"
                              "UV preview. Requires open boundary or seams.");
#else
        menu_cgal_required_item(
            "CGAL LSCM (UV view)...",
            "LSCM angle-preserving UV parameterization with side-by-side UV preview.");
#endif
        if (ImGui::MenuItem("Easy3D Basic..."))
            open_dialog(dlg_sm_parameterization_, st_sm_parameterization_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Easy3D built-in 2D parameterization. Simpler, no UV preview.");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Geodesic / Distance")) {
        if (ImGui::MenuItem("Distance Field... [recommended]"))
            open_dialog(dlg_geo_, st_geo_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Interactive geodesic: Front Propagation (Easy3D) / Exact Shortest\n"
                              "Path / Heat Method (CGAL). Pick sources, view isolines.");
        if (ImGui::MenuItem("Easy3D Quick (vertex 0)")) {
            auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(viewer_.current_model());
            if (claw3d::services::compute_quick_vertex_zero_geodesic(mesh)) {
                mesh->renderer()->update();
                viewer_.mark_dirty();
                LOG(INFO) << "geodesic distances computed from vertex 0";
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quick non-interactive test: distances from vertex 0 to all others.");
        ImGui::EndMenu();
    }
}
