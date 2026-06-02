// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "platform/window_events.h"
#include "services/jobs/easy3d/poisson_reconstruction_job.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "ui/layout_helpers.h"
#include "ui/panel_help.h"
#include "ui/status_widgets.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

static const char* POISSON_HELP_PROMPT = R"(
Explain Poisson Surface Reconstruction in 3D Claw:

**Algorithm**: Screened Poisson surface reconstruction (Kazhdan & Hoppe 2013).
Builds an indicator function from oriented point samples, then extracts an isosurface.

**Parameters** (source: point_cloud_poisson_reconstruction.h):
- depth: Max octree depth. Voxel grid resolution <= 2^depth. Default 8. Memory ~ 4^(depth).
- samples_per_node: Min points per octree node before subdividing. Default 1.0.
  Clean data: 1.0-5.0. Noisy data: 15.0-20.0.
- full_depth: Depth above which octree adapts (default 5).
- cg_depth: Conjugate gradient solver depth (usually don't change).
- scale: Reconstruction scale factor (default 1.1).
- point_weight: Interpolation weight for screened Poisson (default 4.0). 0 = unscreened.
- iso_div: legacy/reserved UI field in this 3D Claw panel. The current
  Poisson wrapper computes the isovalue internally and does not expose
  an iso divider setter, so do not treat this field as an effective parameter.
- gs_iter: Gauss-Seidel iterations (default 8).
- threads: Number of OpenMP threads (default auto).

**Key function**: PoissonReconstruction::apply(PointCloud*) -> SurfaceMesh*
Stores density attribute "v:density" on output mesh vertices.
PoissonReconstruction::trim() can trim low-density regions after reconstruction.

**Quality expectations**:
- Good result: watertight, 0 boundary edges, Hausdorff < 0.5% bbox diagonal
- Depth 6: ~10K faces, fast preview (seconds)
- Depth 8: ~50-200K faces, normal quality (tens of seconds)
- Depth 10: ~500K-2M faces, high quality (minutes, high memory)
- Many holes -> increase depth, or input normals may be inconsistent
- Over-smoothed -> decrease samples_per_node
- Floating blobs -> increase samples_per_node, or use trim()

Keep concise and practical. The user loaded a PLY point cloud and wants to know how to get the best reconstruction.
)";

static std::string build_poisson_metadata_prompt(
    easy3d::PointCloud* cloud, int depth, float spn, int cg_depth, float scale,
    float iso_div) {
    std::ostringstream meta;

    auto normals = cloud->get_vertex_property<easy3d::vec3>("v:normal");
    bool has_normals = normals ? true : false;
    int zero_n = 0, nan_n = 0;
    if (has_normals) {
        for (auto v : cloud->vertices()) {
            const auto& n = normals[v];
            float len2 = n.x*n.x + n.y*n.y + n.z*n.z;
            if (len2 == 0.0f) zero_n++;
            else if (std::isnan(len2)) nan_n++;
        }
    }

    int n_pts = cloud->n_vertices();
    const auto& bbox = cloud->bounding_box();
    float diag = bbox.diagonal_length();
    float max_ext = bbox.max_range();
    float min_ext = bbox.min_range();
    float aspect_ratio = (min_ext > 0.001f) ? max_ext / min_ext : 999.0f;

    float finest_cell = diag * 1.1f / (float)(1 << depth);
    float est_faces = std::min((float)(1 << (2 * depth - 4)), (float)n_pts * 3.0f);
    float est_ram_gb = est_faces * 200.0f / (1024.0f * 1024.0f * 1024.0f);
    float est_time_sec = est_faces / 50000.0f;

    meta << "## Current Model Metadata\n\n"
         << "| Property | Value |\n"
         << "|----------|-------|\n"
         << "| Points | " << n_pts << " |\n"
         << "| Has Normals | " << (has_normals ? "YES" : "**NO -- Poisson will FAIL!**") << " |\n";
    if (has_normals) {
        meta << "| Bad Normals (zero/NaN) | " << zero_n << " / " << nan_n << " |\n";
    }
    meta << "| BBox Min | ("
         << bbox.min_point().x << ", " << bbox.min_point().y << ", " << bbox.min_point().z << ") |\n"
         << "| BBox Max | ("
         << bbox.max_point().x << ", " << bbox.max_point().y << ", " << bbox.max_point().z << ") |\n"
         << "| Diagonal | " << diag << " |\n"
         << "| Aspect Ratio | " << aspect_ratio << ":1";
    if (aspect_ratio > 10.0f)
        meta << " **WARNING: very elongated!**";
    meta << " |\n"
         << "| Finest Voxel at depth=" << depth << " | " << finest_cell << " |\n"
         << "| Est. Output Faces | ~" << (int)est_faces << " |\n"
         << "| Est. RAM | ~" << est_ram_gb << " GB |\n"
         << "| Est. Time | ~" << (int)est_time_sec << "s |\n"
         << "| Current Parameters | depth=" << depth
         << ", samples_per_node=" << spn
         << ", cg_depth=" << cg_depth
         << ", scale=" << scale
         << ", iso_div=" << iso_div << " (legacy/reserved; not applied by current backend) |\n"
         << "\n";

    return meta.str() + "\n---\n\n" + POISSON_HELP_PROMPT + "\n" + ai_lang::directive();
}

static std::string build_poisson_parameter_advice_prompt(
    easy3d::PointCloud* cloud, const PoissonReconstructionState& s) {
    std::ostringstream task;
    task << build_poisson_metadata_prompt(
                cloud, s.depth, s.samples_per_node,
                s.cg_depth, s.scale, s.iso_div)
         << "\n---\n\n"
         << "Task:\n"
         << "Please give AI Parameter Advice for this Poisson run.\n"
         << "- Recommend concrete values for depth, samples_per_node, cg_depth, "
            "and scale.\n"
         << "- Explain the speed / memory / detail trade-off.\n"
         << "- If normals are missing or suspicious, say the user should run "
            "normal estimation / reorientation before reconstruction.\n"
         << "- Do not recommend changing iso_div because this backend "
            "currently computes the isovalue internally.\n"
         << "Keep it concise and practical. " << ai_lang::directive();
    return task.str();
}

static std::string build_poisson_result_ai_task() {
    return std::string(
        "Evaluate the last Poisson reconstruction result.\n"
        "Use the injected quality report and current scene context.\n"
        "Please judge whether the reconstruction is acceptable, mention "
        "watertightness, boundary / non-manifold issues, point-to-mesh error, "
        "runtime, and output complexity. Then suggest 2-3 concrete next "
        "actions or parameter changes. ") + ai_lang::directive();
}

void renderDialogPoissonReconstruction(ViewportCanvas* viewer, PoissonReconstructionState& s, bool& open) {
    prepare_dialog_window(520, 480);
    DIALOG_BODY("Poisson Surface Reconstruction", open) {
        auto pq = prereq_pc_with_normals(viewer);
        auto* cloud = dynamic_cast<easy3d::PointCloud*>(viewer->current_model());
        render_panel_header(
            "Reconstruct a watertight surface mesh from an oriented point cloud.",
            "Dense clouds with reliable normals (laser scans, photogrammetry).",
            [&]() {
                auto* win = MainWindow::instance();
                if (!win || !cloud) return;
                if (!win->ai_chat() || !win->ai_chat()->HasApiKey()) {
                    ImGui::OpenPopup("##no_api_key");
                    return;
                }
                auto prompt = build_poisson_metadata_prompt(
                    cloud, s.depth, s.samples_per_node,
                    s.cg_depth, s.scale, s.iso_div);
                win->send_ai_request(
                    prompt, AICtx_CurrentModel | AICtx_ActivePanel,
                    std::string(),
                    "Ask AI: Poisson parameter advice for current cloud");
            });

        {
            if (ImGui::BeginPopup("##no_api_key")) {
                ImGui::Text("Please set API Key in AI Chat panel first.");
                if (ImGui::Button("Open AI Chat")) {
                    MainWindow::instance()->show_ai_chat();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::InputInt("Depth", &s.depth);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Max octree depth. Resolution <= 2^depth voxels.\n"
                                  "6=preview, 8=normal, 10=high quality (4x memory per +1).");

            ImGui::InputFloat("Samples Per Node", &s.samples_per_node);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Min sample points per octree node.\n"
                                  "1.0-5.0 for clean data, 15.0-20.0 for noisy scans.\n"
                                  "Higher = smoother but may lose detail.");

            ImGui::BeginDisabled();
            ImGui::InputFloat("ISO Divider", &s.iso_div);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Legacy/reserved field. The current Easy3D "
                                  "Poisson backend computes the isovalue "
                                  "internally, so this value is not applied.");

            ImGui::InputInt("CG Depth", &s.cg_depth);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Conjugate gradient solver depth. Usually leave at default 0.");

            ImGui::InputFloat("Scale", &s.scale);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reconstruction scale factor. 1.0 = original scale.\n"
                                  "Decrease if mesh too large, increase if too small.");

            auto* win = MainWindow::instance();
            const bool busy = win && win->algorithm_controller().is_running();
            if (!busy) {
                ImGui::BeginDisabled(!cloud);
                if (ImGui::Button("AI Parameter Advice")) {
                    if (win && cloud)
                        win->send_ai_request(
                            build_poisson_parameter_advice_prompt(cloud, s),
                            AICtx_CurrentModel | AICtx_ActivePanel,
                            std::string(),
                            "Ask AI: tune Poisson parameters for my cloud");
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Send current point-cloud metadata and "
                                      "Poisson parameters to AI for advice.");
                if (win && win->ai_chat() && !win->ai_chat()->HasApiKey()) {
                    claw_ui::same_line_if_fits_text("Set API key in AI Chat");
                    ImGui::TextDisabled("Set API key in AI Chat");
                }
            }

            const bool disable_start = busy;
            if (disable_start)
                ImGui::BeginDisabled();
            prereq_begin(pq);
            if (ImGui::Button("Apply")) {
                if (win && cloud && !win->algorithm_controller().is_running()) {
                    claw3d::services::PoissonReconstructionJobStart request;
                    request.source_cloud = cloud;
                    request.source_handle =
                        (win && win->viewer())
                            ? win->viewer()->model_handle(cloud)
                            : ModelHandle{};
                    request.depth = s.depth;
                    request.samples_per_node = s.samples_per_node;
                    request.cg_depth = s.cg_depth;
                    request.scale = s.scale;
                    request.source_name = cloud->name();
                    request.wake_ui = []() { claw3d::app::wake_event_loop(); };
                    if (!claw3d::services::start_poisson_reconstruction_job(
                            win->algorithm_controller(), request)) {
                        LOG(WARNING) << "Failed to start Poisson reconstruction";
                    }
                }
            }
            prereq_end(pq);
            if (disable_start)
                ImGui::EndDisabled();
            claw_ui::same_line_if_fits_button("Cancel");
            if (ImGui::Button("Cancel")) open = false;

            if (win) {
                claw_ui::render_algorithm_status_panel(
                    win->algorithm_controller(),
                    AlgorithmId::PoissonReconstruction,
                    "Poisson Surface Reconstruction");
            }

            auto* pw = MainWindow::instance();
            if (pw && !pw->algorithm_controller().is_running()
                && pw->algorithm_controller().has_quality_context_for(
                    AlgorithmId::PoissonReconstruction)) {
                ImGui::Spacing();
                if (ImGui::Button("AI Evaluate Result")) {
                    if (pw->ai_chat() && pw->ai_chat()->HasApiKey()) {
                        const std::string quality_context =
                            pw->algorithm_controller().take_quality_context();
                        pw->send_ai_request(
                            build_poisson_result_ai_task(),
                            AICtx_CurrentModel,
                            quality_context,
                            "Ask AI: evaluate Poisson reconstruction result");
                    }
                }
                claw_ui::same_line_if_fits_text("(?)");
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Send the quality report to AI for result evaluation.\n"
                                      "Requires API key configured in AI Chat panel.");
            }
        }
    } DIALOG_END;
}
