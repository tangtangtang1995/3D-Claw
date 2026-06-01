// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PREREQUISITES_H
#define CLAW3D_PREREQUISITES_H

/// Shared precondition checks used before launching dialog-driven operations.

#include <string>
#include "imgui.h"

namespace easy3d { class Model; class SurfaceMesh; class PointCloud; class Graph; }
class ViewportCanvas;

// Result of a prerequisite check: ok=true means the algorithm can run.
// When ok=false, `hint` is the red-text reason shown above the Apply button.
struct Prereq {
    bool        ok;
    std::string hint;

    static Prereq pass()                      { return {true,  {}}; }
    static Prereq fail(std::string h)         { return {false, std::move(h)}; }
};

// ---------- Pre-built checks for the most common requirements ----------
// Each one reads viewer->current_model() and decides whether the algorithm
// applies. The hint text is short on purpose: the user reads it as a
// 1-line "you need X" warning, not a manual.

Prereq prereq_any_model        (ViewportCanvas* viewer);
Prereq prereq_surface_mesh     (ViewportCanvas* viewer);
Prereq prereq_point_cloud      (ViewportCanvas* viewer);
Prereq prereq_pc_with_normals  (ViewportCanvas* viewer);
Prereq prereq_mesh_or_pc       (ViewportCanvas* viewer);   // e.g., alpha-wrap
Prereq prereq_graph_model      (ViewportCanvas* viewer);
Prereq prereq_two_models       (ViewportCanvas* viewer);   // for Align

// ---------- UI helpers ----------
// Emits the red hint and pushes BeginDisabled() when the check fails.
// MUST be paired with prereq_end() right after the gated button.
//
// Usage:
//   prereq_begin(check);
//   if (ImGui::Button("Apply")) { ... };
//   prereq_end(check);
void prereq_begin(const Prereq& p);
void prereq_end  (const Prereq& p);

// Like prereq_begin but ONLY shows the red hint, no BeginDisabled().
// Use this when the dialog already has its own enable/disable logic and
// you just want the user to see the explanatory red text at the top.
void prereq_hint_only(const Prereq& p);

// Set initial window position (centered on main viewport) and size for a
// dialog. Call BEFORE DIALOG_BODY. Uses ImGuiCond_FirstUseEver so the user
// can resize/move and we honor that on subsequent opens.
void prepare_dialog_window(float w, float h);

#endif // CLAW3D_PREREQUISITES_H
