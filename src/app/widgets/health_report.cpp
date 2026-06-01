// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"

#include "ai/ai_context.h"
#include "ai/ai_explainable_item.h"
#include "ai/ai_language.h"
#include "window/main_window.h"
#include "model/model_health.h"
#include "ui/layout_helpers.h"
#include "ui/status_widgets.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/model.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>

#include <string>

#include "imgui.h"


namespace {

void render_health_metric_row(const char* label, int value, int warn_above = -1) {
    ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (warn_above >= 0 && value > warn_above)
        col = claw_ui::status_warning_color();
    else if (warn_above >= 0)
        col = claw_ui::status_success_color();
    ImGui::TextColored(col, "%s: %d", label, value);
}

void render_suggested_actions(MainWindow* win, easy3d::Model* m) {
    if (!win || !m)
        return;

    bool is_mesh = dynamic_cast<easy3d::SurfaceMesh*>(m) != nullptr;
    bool is_cloud = dynamic_cast<easy3d::PointCloud*>(m) != nullptr;

    ImGui::SeparatorText("Suggested Actions");

    auto action_btn = [&](const char* label, const char* id) {
        if (ImGui::Button(label))
            win->open_named_dialog(id);
        claw_ui::same_line_if_fits_width(110.0f);
    };

    if (is_mesh) {
        action_btn("Simplify", "cgal_simplify_mesh");
        action_btn("Smooth", "cgal_smooth_mesh");
        action_btn("Fill Holes", "fill_holes");
        action_btn("Sample", "sample_mesh");
    } else if (is_cloud) {
        action_btn("Poisson", "poisson");
        action_btn("Alpha Wrap", "alpha_wrap");
        action_btn("Normals", "estimate_normals");
    }

    if (ImGui::Button("Ask AI")) {
        win->send_ai_request(
            std::string("Based on [Current Model] (including its Health block "
                        "if present) and [Lineage], suggest 2-4 concrete next "
                        "steps. Name the menu entry, justify it, and call out "
                        "any risk. ") + ai_lang::directive(),
            AICtx_CurrentModel | AICtx_Scene | AICtx_Runtime,
            std::string(),
            "Suggest next steps for current model");
    }
    ImGui::NewLine();
}

const char* model_type_name(easy3d::Model* m) {
    if (dynamic_cast<easy3d::SurfaceMesh*>(m))
        return "SurfaceMesh";
    if (dynamic_cast<easy3d::PointCloud*>(m))
        return "PointCloud";
    if (dynamic_cast<easy3d::Graph*>(m))
        return "Graph";
    return "Unknown";
}

} // namespace


void renderWidgetHealthReport(ViewportCanvas* viewer,
                              HealthReportState& s,
                              bool& open) {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Health Report", &open)) {
        auto* m = viewer ? viewer->current_model() : nullptr;
        if (!m) {
            ImGui::TextDisabled("(no model selected)");
            ImGui::End();
            return;
        }

        if (s.last_model != static_cast<void*>(m)) {
            s.last_model = static_cast<void*>(m);
            ModelHealthRegistry::instance().request_compute(m);
        }

        ImGui::Text("Model: %s", m->name().c_str());
        ImGui::Text("Type: %s", model_type_name(m));
        ImGui::Separator();

        const ModelHealth* h = ModelHealthRegistry::instance().get(m);
        int state = h ? h->state.load() : ModelHealth::NotComputed;

        if (state == ModelHealth::Computing) {
            ImGui::TextColored(claw_ui::status_warning_color(),
                               "Analyzing...");
        } else if (state == ModelHealth::Failed) {
            ImGui::TextColored(claw_ui::status_error_color(),
                "Analysis failed: %s",
                h->error_message.empty() ? "(no detail)" : h->error_message.c_str());
            if (ImGui::Button("Retry")) {
                ModelHealthRegistry::instance().invalidate(m);
                ModelHealthRegistry::instance().request_compute(m);
            }
        } else if (state == ModelHealth::Ready) {
            bool is_mesh = dynamic_cast<easy3d::SurfaceMesh*>(m) != nullptr;
            if (is_mesh) {
                render_health_metric_row("Boundary edges",
                                         h->boundary_edges, 50);
                if (h->boundary_edges > 0) {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::HealthReport;
                    item.item_type = AIExplainItemType::HealthFinding;
                    item.label = "Non-watertight";
                    item.value = std::to_string(h->boundary_edges) +
                        " boundary edges";
                    item.detail = std::to_string(h->boundary_loops) +
                        " boundary loop(s)";
                    item.severity = "warning";
                    ai_hover_for_item("hr_boundary", item, m);
                }
                ImGui::TextDisabled("  in %d loop(s)", h->boundary_loops);

                render_health_metric_row("Degenerate faces",
                                         h->degenerate_faces, 0);
                if (h->degenerate_faces > 0) {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::HealthReport;
                    item.item_type = AIExplainItemType::HealthFinding;
                    item.label = "Degenerate faces";
                    item.value = std::to_string(h->degenerate_faces) + " faces";
                    item.severity = "warning";
                    ai_hover_for_item("hr_degen", item, m);
                }

                render_health_metric_row("Isolated vertices",
                                         h->isolated_vertices, 0);
                render_health_metric_row("Non-manifold vertices",
                                         h->non_manifold_vertices, 0);
                if (h->non_manifold_vertices > 0) {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::HealthReport;
                    item.item_type = AIExplainItemType::HealthFinding;
                    item.label = "Non-manifold vertices";
                    item.value = std::to_string(h->non_manifold_vertices) +
                        " vertices";
                    item.severity = "error";
                    ai_hover_for_item("hr_nman", item, m);
                }

                render_health_metric_row("Connected components",
                                         h->connected_components, 1);
                if (h->connected_components > 1) {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::HealthReport;
                    item.item_type = AIExplainItemType::HealthFinding;
                    item.label = "Multiple components";
                    item.value = std::to_string(h->connected_components) +
                        " connected components";
                    item.detail = "Model may need component extraction or separation.";
                    item.severity = "warning";
                    ai_hover_for_item("hr_comp", item, m);
                }
            }

            ImGui::Separator();
            ImGui::Text("Attributes:");
            ImGui::BulletText("vertex normals: %s",
                              h->has_vertex_normals ? "yes" : "no");
            if (!h->has_vertex_normals) {
                AIExplainableItem item;
                item.panel = AIExplainPanel::HealthReport;
                item.item_type = AIExplainItemType::HealthFinding;
                item.label = "Missing vertex normals";
                item.value = "not available";
                item.severity = "warning";
                ai_hover_for_item("hr_nonorm", item, m);
            }

            ImGui::BulletText("vertex colors:  %s",
                              h->has_vertex_colors ? "yes" : "no");
            if (is_mesh) {
                ImGui::BulletText("face colors:    %s",
                                  h->has_face_colors ? "yes" : "no");
                ImGui::BulletText("uv coords:      %s",
                                  h->has_uv ? "yes" : "no");
                if (!h->has_uv) {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::HealthReport;
                    item.item_type = AIExplainItemType::HealthFinding;
                    item.label = "Missing UV coordinates";
                    item.value = "not available";
                    item.detail = "Textures may not display correctly without UV.";
                    item.severity = "warning";
                    ai_hover_for_item("hr_nouv", item, m);
                }
            }

            ImGui::Text("Memory: ~%.1f MB", h->estimated_memory_mb);
            if (h->estimated_memory_mb > 500.0) {
                AIExplainableItem item;
                item.panel = AIExplainPanel::HealthReport;
                item.item_type = AIExplainItemType::HealthFinding;
                item.label = "Large memory footprint";
                item.value = std::to_string((int)h->estimated_memory_mb) + " MB";
                item.severity = "warning";
                ai_hover_for_item("hr_mem", item, m);
            }

            ImGui::TextDisabled("(analysis took %.2fs)", h->compute_time_sec);
            ImGui::Spacing();
            if (ImGui::SmallButton("Re-analyze")) {
                ModelHealthRegistry::instance().invalidate(m);
                ModelHealthRegistry::instance().request_compute(m);
            }
        } else {
            ImGui::TextDisabled("(queued for analysis)");
        }

        render_suggested_actions(MainWindow::instance(), m);
    }
    ImGui::End();
}
