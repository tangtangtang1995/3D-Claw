// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_CONFIG_H
#define CLAW3D_APP_CONFIG_H

/// Persistent application settings loaded at startup and saved on shutdown.

#include <string>

struct GLFWwindow;
class MainWindow;

namespace claw3d {

struct AppConfig {
    AppConfig();

    std::string api_key;
    float background_color[4];
    bool scene_lighting_enabled;
    bool has_lighting;
    float light_position[4];
    float material_ambient[4];
    float material_specular[4];
    float material_shininess;
};

AppConfig load_app_config(GLFWwindow* window);
void apply_runtime_config(MainWindow& main_window, const AppConfig& config);
void save_app_config(GLFWwindow* window, MainWindow* main_window);

} // namespace claw3d

#endif // CLAW3D_APP_CONFIG_H
