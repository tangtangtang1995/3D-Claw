// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"

#include "ai/ai_explainable_item.h"
#include "history/operation_history.h"
#include "ui/layout_helpers.h"

#include <cstdio>
#include <string>

#include "imgui.h"


void renderWidgetHistory(ViewportCanvas* /*viewer*/, HistoryPanelState& s, bool& open) {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History", &open)) {
        ImGui::Checkbox("Auto-scroll", &s.auto_scroll);
        claw_ui::same_line_if_fits_button("Clear All");
        if (ImGui::SmallButton("Clear All"))
            OperationHistory::instance().clear();
        claw_ui::same_line_if_fits_button("Copy All");
        if (ImGui::SmallButton("Copy All")) {
            std::string all = OperationHistory::instance().format_for_ai(1000);
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::Separator();

        const auto& entries = OperationHistory::instance().entries();
        if (entries.empty()) {
            ImGui::TextDisabled("(no operations recorded yet)");
        } else {
            ImGui::BeginChild("##history_scroll");
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (e.status == OperationStatus::Success)
                    col = claw_ui::status_success_color();
                else if (e.status == OperationStatus::Failed)
                    col = claw_ui::status_error_color();
                else if (e.status == OperationStatus::Skipped)
                    col = claw_ui::status_warning_color();
                ImGui::TextColored(col, "%2zu. %s [%s, %.2fs]",
                    i + 1, e.label.c_str(),
                    operation_status_label(e.status), e.elapsed_sec);
                {
                    AIExplainableItem item;
                    item.panel = AIExplainPanel::History;
                    item.item_type = AIExplainItemType::HistoryEntry;
                    item.label = e.label;
                    item.value = operation_status_label(e.status);
                    item.detail = "Elapsed: " + std::to_string(e.elapsed_sec) + "s";
                    if (!e.status_detail.empty())
                        item.detail += "; " + e.status_detail;
                    if (!e.output_names.empty()) {
                        item.detail += "; output: ";
                        for (std::size_t k = 0; k < e.output_names.size(); ++k) {
                            if (k)
                                item.detail += ", ";
                            item.detail += e.output_names[k];
                            if (k >= 2 && e.output_names.size() > 3) {
                                item.detail += " (+" +
                                    std::to_string(e.output_names.size() - k - 1) +
                                    " more)";
                                break;
                            }
                        }
                    }
                    if (!e.source_name.empty())
                        item.source_hint = e.source_name;
                    if (e.status == OperationStatus::Failed)
                        item.severity = "error";
                    char id_buf[32];
                    std::snprintf(id_buf, sizeof id_buf, "ai_hist_%zu", i);
                    ai_hover_for_item(id_buf, item, nullptr);
                }
                if (!e.source_name.empty())
                    ImGui::BulletText("from: %s", e.source_name.c_str());
                if (!e.output_names.empty()) {
                    std::string outs;
                    for (std::size_t k = 0; k < e.output_names.size(); ++k) {
                        if (k)
                            outs += ", ";
                        outs += e.output_names[k];
                        if (k >= 4 && e.output_names.size() > 5) {
                            char tail[32];
                            std::snprintf(tail, sizeof(tail), " (+%zu more)",
                                          e.output_names.size() - k - 1);
                            outs += tail;
                            break;
                        }
                    }
                    ImGui::BulletText("output: %s", outs.c_str());
                }
                if (!e.status_detail.empty())
                    ImGui::BulletText("status: %s", e.status_detail.c_str());
                ImGui::Spacing();
            }
            if (s.auto_scroll &&
                ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
    }
    ImGui::End();
}
