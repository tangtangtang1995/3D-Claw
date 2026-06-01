// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/basic_dialogs.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ui/layout_helpers.h"
#include "viewport/viewport_canvas.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <cstdio>
#include <string>

void renderDialogProperties(ViewportCanvas* viewer, PropertiesState& s, bool& open) {
    prepare_dialog_window(440, 220);
    DIALOG_BODY("Manipulate Properties", open) {
        auto pq = prereq_any_model(viewer);
        auto* model = viewer->current_model();
        ImGui::Checkbox("Use Manipulator", &s.use_manip);
        ImGui::Separator();
        ImGui::Text("Property command (e.g., v:color = vec3(1,0,0))");
        ImGui::InputText("##cmd", s.command, sizeof(s.command));
        prereq_begin(pq);
        if (ImGui::Button("Apply")) {
            std::string cmd(s.command);
            if (!cmd.empty()) {
                auto eq = cmd.find('=');
                if (eq != std::string::npos) {
                    std::string prop_path = cmd.substr(0, eq);
                    while (!prop_path.empty() && prop_path.back() == ' ') prop_path.pop_back();
                    std::string val_str = cmd.substr(eq + 1);
                    while (!val_str.empty() && val_str.front() == ' ') val_str.erase(0, 1);

                    easy3d::vec3 v3;
                    if (sscanf(val_str.c_str(), "vec3(%f,%f,%f)", &v3[0], &v3[1], &v3[2]) == 3) {
                        auto pos = prop_path.find(':');
                        if (pos != std::string::npos) {
                            std::string loc = prop_path.substr(0, pos);
                            std::string name = prop_path.substr(pos + 1);
                            if (loc == "v") {
                                if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
                                    auto prop = sm->vertex_property<easy3d::vec3>(name);
                                    for (auto v : sm->vertices()) prop[v] = v3;
                                } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(model)) {
                                    auto prop = pc->vertex_property<easy3d::vec3>(name);
                                    for (auto v : pc->vertices()) prop[v] = v3;
                                }
                            } else if (loc == "f") {
                                if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
                                    auto prop = sm->face_property<easy3d::vec3>(name);
                                    for (auto f : sm->faces()) prop[f] = v3;
                                }
                            }
                            model->renderer()->update();
                            viewer->mark_dirty();
                            LOG(INFO) << "set " << cmd;
                        }
                    }
                }
            }
        }
        prereq_end(pq);
        claw_ui::same_line_if_fits_button("Cancel");
        if (ImGui::Button("Cancel")) open = false;
    } DIALOG_END;
}
