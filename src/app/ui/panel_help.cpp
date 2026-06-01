// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ui/panel_help.h"

#include "ui/layout_helpers.h"
#include "imgui.h"
#include "window/main_window.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"


void render_panel_header(const char* purpose,
                         const char* best_for,
                         std::function<void()> on_ai_help) {
    // Style: slightly dimmer body text, "?" button on the same line as the
    // first text row so the header fits in two visual rows total.
    const ImVec4 body_col(0.40f, 0.47f, 0.57f, 1.0f);

    if (purpose && *purpose) {
        ImGui::PushStyleColor(ImGuiCol_Text, body_col);
        ImGui::TextWrapped("Purpose: %s", purpose);
        ImGui::PopStyleColor();
    }
    if (best_for && *best_for) {
        ImGui::PushStyleColor(ImGuiCol_Text, body_col);
        ImGui::TextWrapped("Best for: %s", best_for);
        ImGui::PopStyleColor();
    }

    // AI Help "?" button -- top-right of the header rows.
    claw_ui::same_line_right_if_fits_button("?");
    if (ImGui::SmallButton("?")) {
        if (on_ai_help) {
            on_ai_help();
        } else {
            auto* win = MainWindow::instance();
            if (win) {
                win->send_ai_request(
                    std::string(
                        "Briefly explain the panel listed in [Panel]:\n"
                        "- what it does,\n"
                        "- when to use it,\n"
                        "- typical prerequisites and pitfalls,\n"
                        "- one example use case for the current model if "
                        "[Current Model] is set. ")
                    + ai_lang::directive(),
                    AICtx_ActivePanel | AICtx_CurrentModel,
                    std::string(),
                    "Explain this algorithm panel");
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ask AI to explain this panel based on your current model.\n"
                          "Requires an API key configured in AI Chat.");

    ImGui::Separator();
}
