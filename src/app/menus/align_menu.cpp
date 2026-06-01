// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Align menu section for MainWindow.

#include "window/main_window.h"
#include "menus/ai_menu_utils.h"
#include "viewport/viewport_canvas.h"

#include "imgui.h"


void MainWindow::render_menu_align() {
    if (ImGui::BeginMenu("Align")) {
        menu_group_ai_item(this, "Ask AI: which alignment tool should I use?",
                           "Align",
                           "Choose manual transform, point-pair alignment, or ICP.",
                           "One or two loaded models depending on the alignment mode.",
                           "Apply bakes transforms into geometry; ICP needs rough initial alignment.",
                           viewer_.current_model());
        ImGui::Separator();
        if (ImGui::MenuItem("Transform...")) {
            st_align_.tab = AlignTab::Transform;
            dlg_align_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Translate / Rotate / Scale the current model.\n"
                              "Apply bakes the transform into vertex coordinates.");
        if (ImGui::MenuItem("Point-Pair Alignment...")) {
            st_align_.tab = AlignTab::PointPair;
            dlg_align_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pick N corresponding vertices/points on two models\n"
                              "(via the Select menu), then solve the rigid transform (SVD Kabsch)\n"
                              "that maps Source onto Target.");
        if (ImGui::MenuItem("ICP (Point-to-Point)...")) {
            st_align_.tab = AlignTab::ICP;
            dlg_align_ = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Iterative Closest Point alignment. Works best when the\n"
                              "two models are already roughly aligned -- run Point-Pair first\n"
                              "or use the manipulator to bring them close.");
        ImGui::EndMenu();
    }
}
