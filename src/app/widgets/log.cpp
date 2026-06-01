// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"
#include "ui/layout_helpers.h"

#include <mutex>
#include <vector>

#include "imgui.h"


std::vector<LogEntry> g_log_entries;
std::mutex g_log_mutex;


void renderWidgetLog(ViewportCanvas*, LogState& s, bool& open) {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Log", &open)) {
        ImGui::Checkbox("Auto-scroll", &s.auto_scroll);
        claw_ui::same_line_if_fits_button("Clear");
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            if (ImGui::Button("Clear"))
                g_log_entries.clear();
        }
        ImGui::Separator();
        ImGui::BeginChild("##log_scroll");
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            for (const auto& e : g_log_entries) {
                ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (e.level == "WARNING")
                    c = claw_ui::status_warning_color();
                else if (e.level == "ERROR" || e.level == "FATAL")
                    c = claw_ui::status_error_color();
                ImGui::TextColored(c, "[%s] %s",
                                   e.level.c_str(), e.message.c_str());
            }
        }
        if (s.auto_scroll)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
}
