// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Shared helpers for "Ask AI" affordances in main menu sections.

#include "menus/ai_menu_utils.h"

#include "ai/ai_context.h"
#include "ai/ai_explainable_item.h"
#include "ai/ai_widget.h"
#include "window/main_window.h"

#include "imgui.h"

#include <sstream>
#include <string>


namespace {

std::string menu_ai_detail(const char* summary,
                           const char* requirements,
                           const char* risks,
                           bool opens_dialog,
                           bool has_dialog_ai) {
    std::ostringstream os;
    if (summary && summary[0])
        os << "Summary: " << summary;
    if (requirements && requirements[0])
        os << "\nRequires: " << requirements;
    if (risks && risks[0])
        os << "\nRisk: " << risks;
    os << "\nCommand kind: " << (opens_dialog ? "opens dialog" : "direct action");
    os << "\nDedicated dialog AI: " << (has_dialog_ai ? "yes" : "no");
    return os.str();
}


std::string menu_ai_prompt(const char* path,
                           const char* summary,
                           const char* requirements,
                           const char* risks,
                           bool opens_dialog,
                           bool has_dialog_ai,
                           const easy3d::Model* model) {
    AIExplainableItem item;
    item.panel = AIExplainPanel::MenuBar;
    item.item_type = AIExplainItemType::MenuCommand;
    item.label = path ? path : "Menu command";
    item.value = opens_dialog ? "opens a configuration panel" : "runs immediately";
    item.detail = menu_ai_detail(summary, requirements, risks, opens_dialog, has_dialog_ai);
    item.source_hint = "Main menu";
    if (risks && risks[0])
        item.severity = "notice";
    return build_ai_explain_prompt(item, model);
}

} // namespace


bool menu_command_ai_tip(const char* path,
                         const char* summary,
                         const char* requirements,
                         const char* risks,
                         bool opens_dialog,
                         bool has_dialog_ai,
                         const easy3d::Model* model) {
    std::string id = std::string("menu_ai_") + (path ? path : "unknown");
    // Display label = the menu path itself (e.g. "Point Cloud > Reorient
    // Normals"). The long prompt with [Current Model] / requirements /
    // risks etc. still goes to the API verbatim.
    std::string display = std::string("Explain menu: ") +
                          (path ? path : "(unknown)");
    return ai_hover_tip(id.c_str(), [=]() {
        return menu_ai_prompt(path, summary, requirements, risks,
                              opens_dialog, has_dialog_ai, model);
    }, AICtx_All, display);
}


void menu_group_ai_item(MainWindow* win,
                        const char* label,
                        const char* path,
                        const char* summary,
                        const char* requirements,
                        const char* risks,
                        const easy3d::Model* model) {
    if (ImGui::MenuItem(label)) {
        if (win) {
            const std::string display = std::string("Explain menu: ") + path;
            win->send_ai_request(
                menu_ai_prompt(path, summary, requirements, risks,
                               true, false, model),
                AICtx_All, std::string(), display);
        }
    }
}


void menu_cgal_required_item(const char* label, const char* tooltip) {
    const std::string disabled_label = std::string(label ? label : "Command") +
        " (CGAL required)";
    ImGui::BeginDisabled(true);
    ImGui::MenuItem(disabled_label.c_str(), nullptr, false, false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (tooltip && tooltip[0]) {
            ImGui::SetTooltip(
                "%s\n\nThis command requires a build with "
                "CLAW3D_ENABLE_CGAL=ON.", tooltip);
        }
        else {
            ImGui::SetTooltip(
                "This command requires a build with CLAW3D_ENABLE_CGAL=ON.");
        }
    }
}
