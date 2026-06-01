// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "services/operations/easy3d_model_operations.h"
#include "ui/layout_helpers.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "window/window_helpers.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/dialog.h>
#include <easy3d/util/logging.h>

#include <string>

void renderDialogGaussianNoise(ViewportCanvas* viewer, GaussianNoiseState& s, bool& open) {
    prepare_dialog_window(420, 200);
    DIALOG_BODY("Gaussian Noise", open) {
        auto pq = prereq_mesh_or_pc(viewer);
        auto* model = viewer->current_model();
        ImGui::InputFloat("Sigma", &s.sigma, 0.0f, 0.0f, "%.5f");
        if (model && model->bounding_box().is_valid()) {
            float suggested = model->bounding_box().radius() * 0.01f;
            ImGui::Text("Suggested: %.5f (0.01 * BBox radius)", suggested);
        }
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            claw3d::services::apply_gaussian_noise(model, s.sigma);
            if (model) model->renderer()->update();
            viewer->mark_dirty();
        }
        prereq_end(pq);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}

void renderDialogSnapshot(ViewportCanvas* viewer, SnapshotState& s, bool& open) {
    prepare_dialog_window(380, 280);
    DIALOG_BODY("Snapshot", open) {
        ImGui::InputInt("Width", &s.width);
        ImGui::InputInt("Height", &s.height);
        ImGui::InputInt("Samples", &s.samples);
        const char* bg_items[] = {"Current", "White", "Transparent"};
        ImGui::Combo("Background", &s.background, bg_items, 3);
        ImGui::Checkbox("Expand", &s.expand);
        if (ImGui::Button("Save...")) {
            auto path = easy3d::dialog::save("Save Snapshot", "snapshot.png", {"PNG Files (*.png)", "*.png"});
            if (!path.empty()) {
                bool ok = viewer->snapshot(path, s.width, s.height, s.samples, s.background, s.expand);
                LOG(INFO) << (ok ? "snapshot saved: " + path : "snapshot failed");
            }
        }
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}

void renderDialogWalkThrough(ViewportCanvas*, WalkThroughState&, bool& open) {
    prepare_dialog_window(360, 160);
    DIALOG_BODY("Walk Through", open) {
        ImGui::Text("Alt+Click: add keyframe");
        ImGui::Text("K: add free keyframe");
        if (ImGui::Button("Close")) open = false;
    } DIALOG_END;
}
