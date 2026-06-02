// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include "ui/colormap.h"
#include "ui/walk_through.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "imgui.h"

void ViewportCanvas::render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar);

    viewport_hovered_ = ImGui::IsWindowHovered();
    viewport_focused_ = ImGui::IsWindowFocused();

    ImVec2 size = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(size.x);
    int h = static_cast<int>(size.y);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    const std::uintptr_t texture = render_scene_texture(w, h);

    if (texture != 0) {
        ImGui::Image(static_cast<ImTextureID>(texture),
                     size, ImVec2(0, 1), ImVec2(1, 0));
        viewport_min_x_ = ImGui::GetItemRectMin().x;
        viewport_min_y_ = ImGui::GetItemRectMin().y;
        viewport_max_x_ = ImGui::GetItemRectMax().x;
        viewport_max_y_ = ImGui::GetItemRectMax().y;

        draw_axes_gizmo();

        if (!models_.empty()) {
            auto* m = current_model();
            if (m) {
                auto* pc = dynamic_cast<easy3d::PointCloud*>(m);
                if (pc) {
                    auto dist = pc->get_vertex_property<float>("v:dist");
                    auto drawable =
                        pc->renderer()->get_points_drawable("vertices");
                    if (dist && drawable &&
                        drawable->coloring_method() ==
                            easy3d::State::SCALAR_FIELD) {
                        float dmin = 1e30f, dmax = -1e30f;
                        for (auto v : pc->vertices()) {
                            float d = dist[v];
                            if (d < dmin) dmin = d;
                            if (d > dmax) dmax = d;
                        }
                        if (dmax - dmin < 1e-6f) dmax = dmin + 1e-6f;

                        ImVec2 vp = ImGui::GetWindowPos();
                        ImVec2 vs = ImGui::GetWindowSize();
                        float bar_w = 18, bar_x = vp.x + vs.x - bar_w - 22;
                        float bar_h = (vs.y - 80) / 3.0f;
                        float bar_top = vp.y + 40;
                        float bar_bottom = bar_top + bar_h;

                        ImDrawList* dl = ImGui::GetForegroundDrawList();

                        dl->AddRectFilled(
                            ImVec2(bar_x - 2, bar_top - 12),
                            ImVec2(bar_x + bar_w + 38, bar_bottom + 8),
                            IM_COL32(0, 0, 0, 160), 4.0f);

                        int nbins = 60;
                        std::vector<int> bins(nbins, 0);
                        for (auto v : pc->vertices()) {
                            const int bi = static_cast<int>(
                                (dist[v] - dmin) / (dmax - dmin) *
                                (nbins - 1));
                            if (bi >= 0 && bi < nbins) bins[bi]++;
                        }
                        int max_count = 1;
                        for (int c : bins) if (c > max_count) max_count = c;

                        float hist_w = 30, hist_max_h = bar_h;
                        for (int i = 0; i < nbins; i++) {
                            const float hh = (max_count > 0)
                                ? (static_cast<float>(bins[i]) / max_count *
                                   hist_max_h)
                                : 0.0f;
                            float y = bar_bottom - hh;
                            float x = bar_x - hist_w - 4;
                            const float bx =
                                x + static_cast<float>(i) / nbins * hist_w;
                            const float bw =
                                hist_w / static_cast<float>(nbins);
                            const float t =
                                static_cast<float>(i) / (nbins - 1);
                            ImU32 col = claw_ui::colormap_color(t);
                            col = IM_COL32((col >> 16) & 0xFF,
                                           (col >> 8) & 0xFF,
                                           col & 0xFF, 200);
                            dl->AddRectFilled(ImVec2(bx, y),
                                              ImVec2(bx + bw, bar_bottom),
                                              col);
                        }

                        for (int i = 0; i < static_cast<int>(bar_h); i++) {
                            const float t =
                                1.0f - static_cast<float>(i) / bar_h;
                            dl->AddRectFilled(
                                ImVec2(bar_x, bar_top + i),
                                ImVec2(bar_x + bar_w, bar_top + i + 1),
                                claw_ui::colormap_color(t));
                        }

                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.4f", dmax);
                        dl->AddText(ImVec2(bar_x + bar_w + 4, bar_top - 4),
                                    IM_COL32(255, 255, 255, 200), buf);
                        snprintf(buf, sizeof(buf), "%.4f", dmin);
                        dl->AddText(ImVec2(bar_x + bar_w + 4, bar_bottom - 10),
                                    IM_COL32(255, 255, 255, 200), buf);
                    }
                }
            }
        }
    }

    if (rect_dragging_) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 r_min(rect_start_x_ < rect_end_x_ ? rect_start_x_ : rect_end_x_,
                     rect_start_y_ < rect_end_y_ ? rect_start_y_ : rect_end_y_);
        ImVec2 r_max(rect_start_x_ < rect_end_x_ ? rect_end_x_ : rect_start_x_,
                     rect_start_y_ < rect_end_y_ ? rect_end_y_ : rect_start_y_);
        dl->AddRectFilled(r_min, r_max, IM_COL32(255, 220, 50, 50));
        dl->AddRect(r_min, r_max, IM_COL32(255, 220, 50, 200),
                    0.0f, 0, 1.5f);
    }

    if (walk_through_ && walk_through_->interpolator()->is_interpolation_started())
        dirty_ = true;

    ImGui::End();
    ImGui::PopStyleVar();
}
