// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_UI_SCALE_H
#define CLAW3D_UI_SCALE_H

/// Central UI scaling helpers for DPI-aware ImGui dimensions.

#include "imgui.h"

// Global Dear ImGui UI scale (font + style sizes).
//
// Stored once at startup as a singleton. Every frame, BEFORE
// ImGui::NewFrame(), the caller restores the style to the captured
// baseline and applies ScaleAllSizes(scale_), then sets
// io.FontGlobalScale = scale_. This keeps all paddings, frame borders,
// item spacing, etc. coherent with the font, instead of scaling only
// the glyphs (which makes the layout look weirdly sparse).
class UIScale {
public:
    static UIScale& instance();

    // Save baseline. Call once at startup AFTER the application has
    // set its desired style defaults but BEFORE any frame is drawn.
    // Subsequent set_scale + apply_for_next_frame will rebuild the
    // live style by ScaleAllSizes() of this baseline.
    void capture_baseline();

    // Clamped to [0.7, 2.0]. Anything outside breaks dock layouts.
    void set_scale(float s);
    float scale() const { return scale_; }

    static constexpr float kMin = 0.7f;
    static constexpr float kMax = 2.0f;

    // Call every frame BEFORE ImGui::NewFrame().
    void apply_for_next_frame();

private:
    UIScale() = default;
    UIScale(const UIScale&) = delete;
    UIScale& operator=(const UIScale&) = delete;

    float      scale_ = 1.0f;
    ImGuiStyle baseline_{};   // captured once at startup
    bool       has_baseline_ = false;
};

#endif // CLAW3D_UI_SCALE_H
