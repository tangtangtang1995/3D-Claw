// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/measurement_dialog.h"

#include <cstdio>
#include <cmath>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>

#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "services/operations/easy3d_model_operations.h"
#include "ui/layout_helpers.h"

void MeasurementState::reset() {
    points.clear();
    history.clear();
    picking = false;
    next_label = 0;
    overlay_dirty = true;
}

void MeasurementState::clear_history() {
    history.clear();
    overlay_dirty = true;
}

int MeasurementState::expected_points() const {
    switch (type) {
        case MeasureType::Distance:  return 2;
        case MeasureType::Angle:     return 3;
        case MeasureType::Polyline:  return -1;
        default: return 0;
    }
}

void MeasurementState::add_point(const easy3d::vec3& p) {
    MeasurePoint mp;
    mp.pos = p;
    snprintf(mp.label, sizeof(mp.label), "P%d", next_label++);
    points.push_back(mp);
    overlay_dirty = true;

    int exp = expected_points();
    if (exp > 0 && (int)points.size() >= exp)
        finalize_current();
}

void MeasurementState::finalize_current() {
    if (points.empty()) return;

    MeasurementResult r;
    r.type = type;
    r.points = points;
    bool valid = false;

    switch (type) {
        case MeasureType::Distance:
            if (points.size() >= 2) {
                r.value = easy3d::distance(points[0].pos, points[1].pos);
                snprintf(r.label, sizeof(r.label), "%s-%s: %.4f",
                         points[0].label, points[1].label, r.value);
                valid = true;
            }
            break;
        case MeasureType::Polyline:
            if (points.size() >= 2) {
                r.value = 0.0f;
                for (size_t i = 1; i < points.size(); ++i)
                    r.value += easy3d::distance(points[i-1].pos, points[i].pos);
                snprintf(r.label, sizeof(r.label),
                         "Polyline (%zu pts): %.4f",
                         points.size(), r.value);
                valid = true;
            }
            break;
        case MeasureType::Angle:
            if (points.size() >= 3) {
                auto ba = points[0].pos - points[1].pos;
                auto bc = points[2].pos - points[1].pos;
                float len_ba = easy3d::length(ba);
                float len_bc = easy3d::length(bc);
                if (len_ba > 0 && len_bc > 0) {
                    float d = easy3d::dot(ba, bc) / (len_ba * len_bc);
                    if (d > 1.0f) d = 1.0f;
                    if (d < -1.0f) d = -1.0f;
                    r.value = std::acos(d) * 180.0f / 3.14159265f;
                    snprintf(r.label, sizeof(r.label),
                             "Angle %s%s%s: %.2f deg",
                             points[0].label, points[1].label,
                             points[2].label, r.value);
                    valid = true;
                }
            }
            break;
        default: break;
    }

    if (valid)
        history.push_back(r);
    points.clear();         // ready for the next group; picking stays ON
    overlay_dirty = true;
}


static bool is_interactive(MeasureType t) {
    return t == MeasureType::Distance || t == MeasureType::Polyline || t == MeasureType::Angle;
}


void renderDialogMeasurement(ViewportCanvas* viewer, MainWindow* win,
                             MeasurementState& s, bool& open) {
    prepare_dialog_window(480, 500);
    DIALOG_BODY("Measurement", open) {
        prereq_hint_only(prereq_any_model(viewer));
        if (s.overlay_dirty) {
            win->update_measurement_overlay(s);
            s.overlay_dirty = false;
        }
        const char* type_names[] = {"Distance", "Polyline", "Angle",
                                     "Bounding Box", "Surface Area", "Volume"};
        int cur_type = (int)s.type;
        if (ImGui::Combo("Type", &cur_type, type_names, 6)) {
            if (cur_type != (int)s.type) {
                s.reset();
                s.type = (MeasureType)cur_type;
            }
        }

        if (is_interactive(s.type)) {
            ImGui::Checkbox("Snap to vertex", &s.snap_to_vertex);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "On (default): clicking anywhere on a mesh face snaps to\n"
                    "that face's nearest vertex (easy to hit small details).\n"
                    "Off: the click records the exact surface point under the\n"
                    "cursor (useful for non-vertex measurements).\n"
                    "Point clouds always snap to the nearest point.");
        }

        ImGui::Separator();

        if (is_interactive(s.type)) {
            // Start/Stop continuous picking. Distance/Angle auto-finalize
            // each group, push into history, and immediately accept the
            // next group -- picking stays on until the user clicks Stop.
            if (!s.picking) {
                if (ImGui::Button("Start Picking")) {
                    s.points.clear();        // drop any half-finished group
                    s.picking = true;
                    s.type = (MeasureType)cur_type;
                    s.overlay_dirty = true;
                    LOG(INFO) << "measurement: start picking ("
                              << (s.type == MeasureType::Distance ? "2 pts/group"
                                  : s.type == MeasureType::Angle ? "3 pts/group"
                                  : "polyline -- click Finish to end one")
                              << ")";
                }
            } else {
                if (ImGui::Button("Stop Picking")) {
                    s.picking = false;
                    s.points.clear();
                    s.overlay_dirty = true;
                }
                claw_ui::same_line_if_fits_button("Finish Polyline");
                if (s.type == MeasureType::Polyline) {
                    if (ImGui::Button("Finish Polyline")) {
                        s.finalize_current();
                    }
                }
            }

            // -- In-progress points --
            if (!s.points.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("In progress:");
                for (auto& p : s.points)
                    ImGui::BulletText("%s: (%.3f, %.3f, %.3f)",
                                      p.label, p.pos.x, p.pos.y, p.pos.z);
                if (s.picking && s.type == MeasureType::Polyline)
                    ImGui::TextDisabled("Click to add more points; "
                                        "Finish Polyline closes this run.");
            }

            // -- History (all finalized measurements this session) --
            if (!s.history.empty()) {
                ImGui::Spacing();
                ImGui::SeparatorText("Results");
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      claw_ui::status_success_color());
                for (size_t i = 0; i < s.history.size(); ++i) {
                    const auto& r = s.history[i];
                    ImGui::Text("%2zu. %s", i + 1, r.label);
                }
                ImGui::PopStyleColor();
            }
        } else {
            // Non-interactive: instant model info. No history concept --
            // value is recomputed every frame from the current model.
            char inst_label[80] = "";
            auto* model = viewer->current_model();
            if (!model) {
                ImGui::TextDisabled("No model loaded");
            } else {
                switch (s.type) {
                    case MeasureType::MtBBox: {
                        auto bb = model->bounding_box();
                        auto diag = bb.max_point() - bb.min_point();
                        ImGui::Text("Model: %s", model->name().c_str());
                        ImGui::Text("X: %.4f | Y: %.4f | Z: %.4f", diag.x, diag.y, diag.z);
                        float d = easy3d::length(diag);
                        ImGui::Text("Diagonal: %.4f", d);
                        snprintf(inst_label, sizeof(inst_label),
                                 "BBox: %.4f x %.4f x %.4f (diag %.4f)",
                                 diag.x, diag.y, diag.z, d);
                        break;
                    }
                    case MeasureType::MtSurfaceArea: {
                        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
                        if (mesh) {
                            float area = claw3d::services::compute_surface_area(mesh);
                            ImGui::Text("Model: %s (F:%d)", mesh->name().c_str(), mesh->n_faces());
                            ImGui::PushStyleColor(ImGuiCol_Text, claw_ui::status_success_color());
                            ImGui::Text("Surface Area: %.4f", area);
                            ImGui::PopStyleColor();
                            snprintf(inst_label, sizeof(inst_label),
                                     "Surface Area: %.4f", area);
                        } else {
                            ImGui::TextDisabled("Current model is not a SurfaceMesh");
                        }
                        break;
                    }
                    case MeasureType::MtVolume: {
                        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
                        if (mesh) {
                            float vol = claw3d::services::compute_volume(mesh);
                            ImGui::Text("Model: %s", mesh->name().c_str());
                            ImGui::Text("Closed: %s", mesh->is_closed() ? "YES" : "NO");
                            if (mesh->is_closed()) {
                                ImGui::PushStyleColor(ImGuiCol_Text, claw_ui::status_success_color());
                                ImGui::Text("Volume: %.4f", vol);
                                ImGui::PopStyleColor();
                            } else {
                                ImGui::TextColored(claw_ui::status_warning_color(),
                                                   "Volume: %.4f (unreliable - mesh is open)", vol);
                            }
                            snprintf(inst_label, sizeof(inst_label),
                                     "Volume: %.4f%s", vol, mesh->is_closed() ? "" : " [open mesh]");
                        } else {
                            ImGui::TextDisabled("Current model is not a SurfaceMesh");
                        }
                        break;
                    }
                    default: break;
                }
            }

            ImGui::Separator();
            if (inst_label[0]) {
                if (ImGui::Button("Copy"))
                    ImGui::SetClipboardText(inst_label);
            }
            DIALOG_END;
            return;
        }

        // -- Bottom action bar (interactive types only) --
        ImGui::Separator();
        bool any = !s.history.empty() || !s.points.empty();
        if (!s.history.empty()) {
            if (ImGui::Button("Copy Last"))
                ImGui::SetClipboardText(s.history.back().label);
            claw_ui::same_line_if_fits_button("Copy All");
            if (ImGui::Button("Copy All")) {
                std::string all;
                for (auto& r : s.history) { all += r.label; all += '\n'; }
                ImGui::SetClipboardText(all.c_str());
            }
            claw_ui::same_line_if_fits_button("Clear All");
        }
        if (any) {
            if (ImGui::Button("Clear All")) {
                s.reset();
                win->clear_measurement_overlay();
            }
        }
        if (s.picking && !s.points.empty()) {
            claw_ui::same_line_if_fits_button("Undo Last Point");
            if (ImGui::Button("Undo Last Point")) {
                s.points.pop_back();
                if (s.next_label > 0) --s.next_label;
                s.overlay_dirty = true;
            }
        }
    }
    DIALOG_END;
}
