// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"

#include "viewport/viewport_canvas.h"
#include "product_identity.h"
#include "ui/layout_helpers.h"
#include "ui/ui_scale.h"
#include "viewport/scene_lighting.h"

#include <easy3d/core/model.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>
#include <easy3d/util/setting.h>

#include <cmath>

#include "imgui.h"

namespace {

easy3d::vec4 default_light_position() {
    return claw3d::default_scene_light_position();
}

easy3d::vec4 default_material_ambient() {
    return claw3d::default_scene_material_ambient();
}

easy3d::vec4 default_material_specular() {
    return claw3d::default_scene_material_specular();
}

float default_material_shininess() {
    return claw3d::default_scene_material_shininess();
}

easy3d::State::Material current_scene_material() {
    return easy3d::State::Material(easy3d::setting::material_ambient,
                                   easy3d::setting::material_specular,
                                   easy3d::setting::material_shininess);
}

template <typename Drawables>
void apply_material_to_lit_drawables(const Drawables& drawables,
                                     const easy3d::State::Material& material) {
    for (const auto& drawable : drawables) {
        if (drawable && drawable->lighting())
            drawable->set_material(material);
    }
}

void apply_scene_material_to_models(ViewportCanvas* viewer) {
    if (!viewer)
        return;

    const auto material = current_scene_material();
    for (const auto& model : viewer->models()) {
        if (!model || !model->renderer())
            continue;

        auto* renderer = model->renderer();
        apply_material_to_lit_drawables(renderer->points_drawables(), material);
        apply_material_to_lit_drawables(renderer->lines_drawables(), material);
        apply_material_to_lit_drawables(renderer->triangles_drawables(), material);
    }
}

void apply_scene_lighting_to_models(ViewportCanvas* viewer, bool enabled) {
    if (!viewer)
        return;

    for (const auto& model : viewer->models()) {
        if (!model || !model->renderer())
            continue;
        for (const auto& drawable : model->renderer()->triangles_drawables()) {
            if (drawable)
                drawable->set_lighting(enabled);
        }
    }
    viewer->mark_dirty();
}

void set_scene_material(ViewportCanvas* viewer,
                        const easy3d::vec4& ambient,
                        const easy3d::vec4& specular,
                        float shininess) {
    easy3d::setting::material_ambient = ambient;
    easy3d::setting::material_specular = specular;
    easy3d::setting::material_shininess = shininess;
    apply_scene_material_to_models(viewer);
    if (viewer)
        viewer->mark_dirty();
}

bool normalize_light_direction() {
    auto& light = easy3d::setting::light_position;
    const float len = std::sqrt(light[0] * light[0] + light[1] * light[1] +
                                light[2] * light[2]);
    if (len <= 1.0e-6f)
        return false;

    light = easy3d::vec4(light[0] / len, light[1] / len, light[2] / len, 0.0f);
    return true;
}

void render_lighting_controls(ViewportCanvas* viewer) {
    ImGui::SeparatorText("Lighting");

    bool lighting_enabled = claw3d::scene_lighting_enabled();
    if (ImGui::Checkbox("Enable Surface Lighting", &lighting_enabled)) {
        claw3d::set_scene_lighting_enabled(lighting_enabled);
        apply_scene_lighting_to_models(viewer, lighting_enabled);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Controls whether scene surface meshes use Easy3D lighting.\n"
            "Default is on; specular highlights are off by default.");

    auto light = easy3d::setting::light_position;
    float light_dir[3] = {light[0], light[1], light[2]};
    if (ImGui::SliderFloat3("Light Direction", light_dir, -1.0f, 1.0f,
                            "%.2f")) {
        easy3d::setting::light_position =
            easy3d::vec4(light_dir[0], light_dir[1], light_dir[2], 0.0f);
        if (viewer)
            viewer->mark_dirty();
    }

    float ambient[3] = {easy3d::setting::material_ambient[0],
                        easy3d::setting::material_ambient[1],
                        easy3d::setting::material_ambient[2]};
    float specular[3] = {easy3d::setting::material_specular[0],
                         easy3d::setting::material_specular[1],
                         easy3d::setting::material_specular[2]};
    float shininess = easy3d::setting::material_shininess;

    bool material_changed = false;
    material_changed |= ImGui::ColorEdit3("Ambient", ambient);
    material_changed |= ImGui::ColorEdit3("Specular", specular);
    material_changed |= ImGui::SliderFloat("Shininess", &shininess,
                                           1.0f, 128.0f, "%.0f");
    if (material_changed) {
        set_scene_material(
            viewer,
            easy3d::vec4(ambient[0], ambient[1], ambient[2], 1.0f),
            easy3d::vec4(specular[0], specular[1], specular[2], 1.0f),
            shininess);
    }

    if (ImGui::SmallButton("Normalize Light")) {
        if (normalize_light_direction() && viewer)
            viewer->mark_dirty();
    }
    claw_ui::same_line_if_fits_button("Reset Lighting");
    if (ImGui::SmallButton("Reset Lighting")) {
        claw3d::set_scene_lighting_enabled(true);
        apply_scene_lighting_to_models(viewer, true);
        easy3d::setting::light_position = default_light_position();
        set_scene_material(viewer,
                           default_material_ambient(),
                           default_material_specular(),
                           default_material_shininess());
    }
}

} // namespace


void renderSettingsDialog(ViewportCanvas* viewer, bool& open) {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(400, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Display Settings", &open)) {
        auto bg = viewer->background_color();
        float arr[4] = {bg[0], bg[1], bg[2], bg[3]};
        if (ImGui::ColorEdit4("Background", arr)) {
            viewer->set_background_color(easy3d::vec4(arr[0], arr[1], arr[2], arr[3]));
            viewer->mark_dirty();
        }

        render_lighting_controls(viewer);

        ImGui::SeparatorText("UI Scale");
        float ui = UIScale::instance().scale();
        int percent = static_cast<int>(std::round(ui * 100.0f));
        if (ImGui::SliderInt("Scale", &percent,
                             static_cast<int>(UIScale::kMin * 100),
                             static_cast<int>(UIScale::kMax * 100),
                             "%d%%")) {
            UIScale::instance().set_scale(percent * 0.01f);
        }
        claw_ui::same_line_if_fits_button("Reset");
        if (ImGui::SmallButton("Reset"))
            UIScale::instance().set_scale(1.0f);
        ImGui::TextDisabled("Shortcuts: Ctrl+= bigger, Ctrl+- smaller, Ctrl+0 reset.");
        ImGui::TextDisabled("Setting persists in %s.", product_identity::kConfigFileName);
    }
    ImGui::End();
}
