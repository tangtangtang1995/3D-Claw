// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_widget.h"
#include "window/main_window.h"
#include "ai/ai_context.h"
#include "ui/status_colors.h"

#include <easy3d/util/logging.h>
#include <imgui_internal.h>


namespace {
    constexpr float kAnchorSlotWidth = 28.0f;
    constexpr float kAnchorSize = 18.0f;
    constexpr float kAnchorPadding = 4.0f;

    bool current_window_or_parent_hovered() {
        ImGuiWindow* current = ImGui::GetCurrentWindow();
        for (ImGuiWindow* w = GImGui->HoveredWindow; w; w = w->ParentWindow) {
            if (w == current)
                return true;
        }
        return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows
            | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    }
}


bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder)
{
    return ai_hover_tip(unique_id, prompt_builder,
        AICtx_CurrentModel | AICtx_ActivePanel, std::string());
}


bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder,
                  unsigned ctx_flags)
{
    return ai_hover_tip(unique_id, prompt_builder, ctx_flags, std::string());
}


bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder,
                  unsigned ctx_flags,
                  const std::string& display_label)
{
    if (ImGui::GetCurrentWindow()->SkipItems)
        return false;

    ImVec2 row_min = ImGui::GetItemRectMin();
    ImVec2 row_max = ImGui::GetItemRectMax();

    const float content_right =
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float anchor_x = content_right - kAnchorSize - kAnchorPadding;
    const float anchor_y = row_min.y + (row_max.y - row_min.y - kAnchorSize) * 0.5f;

    ImRect anchor_rect(ImVec2(anchor_x, anchor_y),
                       ImVec2(anchor_x + kAnchorSize, anchor_y + kAnchorSize));
    ImRect hover_rect(row_min, row_max);
    hover_rect.Max.x = anchor_rect.Max.x + kAnchorSlotWidth;
    hover_rect.Min.y -= 1.0f;
    hover_rect.Max.y += 1.0f;

    ImVec2 mouse = ImGui::GetMousePos();
    const bool row_hovered =
        (hover_rect.Contains(mouse) || anchor_rect.Contains(mouse)) &&
        current_window_or_parent_hovered();

    if (!row_hovered)
        return false;

    // This intentionally uses ImGui internals to draw a non-layout-affecting
    // row anchor. Re-test this helper whenever Dear ImGui is upgraded.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 old_cursor = window->DC.CursorPos;
    const ImVec2 old_cursor_prev_line = window->DC.CursorPosPrevLine;
    const ImVec2 old_cursor_max = window->DC.CursorMaxPos;
    const ImVec2 old_curr_line_size = window->DC.CurrLineSize;
    const ImVec2 old_prev_line_size = window->DC.PrevLineSize;
    const float old_curr_line_text_base_offset = window->DC.CurrLineTextBaseOffset;
    const float old_prev_line_text_base_offset = window->DC.PrevLineTextBaseOffset;
    const bool old_is_same_line = window->DC.IsSameLine;
    const bool old_is_set_pos = window->DC.IsSetPos;

    ImGui::PushID(unique_id);
    ImGui::SetCursorScreenPos(anchor_rect.Min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleColor(ImGuiCol_Text, claw_ui::status_running_color());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.60f, 1.0f, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f, 0.50f, 1.0f, 0.70f));
    bool clicked = ImGui::Button("?##ai_tip", ImVec2(kAnchorSize, kAnchorSize));
    const bool anchor_hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(4);

    window->DC.CursorPos = old_cursor;
    window->DC.CursorPosPrevLine = old_cursor_prev_line;
    window->DC.CursorMaxPos = old_cursor_max;
    window->DC.CurrLineSize = old_curr_line_size;
    window->DC.PrevLineSize = old_prev_line_size;
    window->DC.CurrLineTextBaseOffset = old_curr_line_text_base_offset;
    window->DC.PrevLineTextBaseOffset = old_prev_line_text_base_offset;
    window->DC.IsSameLine = old_is_same_line;
    window->DC.IsSetPos = old_is_set_pos;

    if (anchor_hovered)
        ImGui::SetTooltip("Ask AI about this");

    if (clicked) {
        LOG(INFO) << "AI hover-tip clicked: " << unique_id;
        auto* win = MainWindow::instance();
        if (win) {
            std::string prompt = prompt_builder();
            win->show_ai_chat();
            // The full prompt goes to the AI; display_label (if any) is
            // what shows up in the chat panel; see ChatMessage.
            win->send_ai_request(prompt, ctx_flags, std::string(),
                                 display_label);
        }
    }
    ImGui::PopID();
    return clicked;
}
