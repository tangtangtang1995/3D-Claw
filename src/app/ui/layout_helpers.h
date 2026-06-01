// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_LAYOUT_HELPERS_H
#define CLAW3D_LAYOUT_HELPERS_H

/// Small ImGui layout helpers for responsive panel rows and wrapping labels.

#include "ui/status_colors.h"

#include "imgui.h"

#include <cstring>

namespace claw_ui {

inline const char* visible_label_end(const char* label) {
    if (!label)
        return "";
    for (const char* p = label; *p; ++p) {
        if (p[0] == '#' && p[1] == '#')
            return p;
    }
    return label + std::strlen(label);
}

inline float button_width_for_label(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* text = label ? label : "";
    const char* end = visible_label_end(text);
    return ImGui::CalcTextSize(text, end).x + style.FramePadding.x * 2.0f;
}

inline float text_width_for_label(const char* label) {
    const char* text = label ? label : "";
    const char* end = visible_label_end(text);
    return ImGui::CalcTextSize(text, end).x;
}

inline void same_line_if_fits_width(float next_item_width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float right =
        ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + spacing + next_item_width <= right)
        ImGui::SameLine();
}

inline void same_line_if_fits_button(const char* label) {
    same_line_if_fits_width(button_width_for_label(label));
}

inline void same_line_if_fits_text(const char* label) {
    same_line_if_fits_width(text_width_for_label(label));
}

inline void same_line_right_if_fits_width(float next_item_width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float right =
        ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
    const float item_start = right - next_item_width;
    if (avail > next_item_width &&
        ImGui::GetItemRectMax().x + style.ItemSpacing.x <= item_start)
        ImGui::SameLine(ImGui::GetCursorPosX() + avail - next_item_width);
}

inline void same_line_right_if_fits_button(const char* label) {
    same_line_right_if_fits_width(button_width_for_label(label));
}

inline void same_line_right_if_fits_text(const char* label) {
    same_line_right_if_fits_width(text_width_for_label(label));
}

} // namespace claw_ui

#endif // CLAW3D_LAYOUT_HELPERS_H
