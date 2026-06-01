// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "bootstrap/app_config.h"

#include "ai/ai_chat.h"
#include "ai/ai_language.h"
#include "window/main_window.h"
#include "product_identity.h"
#include "services/platform/platform_paths.h"
#include "ui/ui_scale.h"
#include "viewport/scene_lighting.h"
#include "viewport/viewport_canvas.h"

#include <3rd_party/json/json.hpp>

#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/setting.h>

#include <GLFW/glfw3.h>

#include <fstream>

using json = nlohmann::json;

namespace claw3d {

namespace {

constexpr int kSceneMaterialVersion = 1;

bool read_float_array4(const json& data, const char* key, float out[4]) {
    if (!data.contains(key) || !data[key].is_array() ||
        data[key].size() != 4) {
        return false;
    }

    for (int i = 0; i < 4; ++i)
        out[i] = data[key][i].get<float>();
    return true;
}

void copy_vec4_to_array(const easy3d::vec4& value, float out[4]) {
    for (int i = 0; i < 4; ++i)
        out[i] = value[i];
}

} // namespace

AppConfig::AppConfig()
    : api_key()
    , scene_lighting_enabled(true)
    , has_lighting(false)
    , material_shininess(default_scene_material_shininess()) {
    background_color[0] = -1.0f;
    background_color[1] = -1.0f;
    background_color[2] = -1.0f;
    background_color[3] = -1.0f;
    copy_vec4_to_array(default_scene_light_position(), light_position);
    copy_vec4_to_array(default_scene_material_ambient(), material_ambient);
    copy_vec4_to_array(default_scene_material_specular(), material_specular);
}

AppConfig load_app_config(GLFWwindow* window) {
    AppConfig config;

    const std::string config_path =
        platform::app_config_path(product_identity::kConfigFileName);
    const std::string executable_dir = easy3d::file_system::executable_directory();
    const std::string executable_config_path =
        executable_dir + "/" + product_identity::kConfigFileName;

    std::ifstream file;
    if (!config_path.empty())
        file.open(config_path);
    if (!file.is_open()) {
        file.clear();
        file.open(executable_config_path);
    }
    if (!file.is_open())
        return config;

    try {
        json data = json::parse(file);
        if (data.contains("window_x") && data.contains("window_y"))
            glfwSetWindowPos(window, data["window_x"], data["window_y"]);
        if (data.contains("window_w") && data.contains("window_h"))
            glfwSetWindowSize(window, data["window_w"], data["window_h"]);
        if (data.contains("ui_scale"))
            UIScale::instance().set_scale((float)data["ui_scale"]);
        if (data.contains("ai_language"))
            ai_lang::set(ai_lang::from_id(data["ai_language"].get<std::string>()));
        if (data.contains("api_key"))
            config.api_key = data["api_key"].get<std::string>();
        if (data.contains("scene_lighting_enabled"))
            config.scene_lighting_enabled =
                data["scene_lighting_enabled"].get<bool>();
        const bool has_current_scene_material =
            data.value("scene_material_version", 0) >= kSceneMaterialVersion;
        read_float_array4(data, "background_color", config.background_color);
        const bool has_light =
            read_float_array4(data, "light_position", config.light_position);
        const bool has_ambient =
            read_float_array4(data, "material_ambient", config.material_ambient);
        const bool has_specular =
            read_float_array4(data, "material_specular", config.material_specular);
        if (has_light && has_ambient && has_specular &&
            data.contains("material_shininess")) {
            config.material_shininess = data["material_shininess"].get<float>();
            if (!has_current_scene_material) {
                copy_vec4_to_array(default_scene_material_specular(),
                                   config.material_specular);
                config.material_shininess = default_scene_material_shininess();
            }
            config.has_lighting = true;
        }
    } catch (const std::exception& e) {
        LOG(WARNING) << "Ignoring invalid config file: " << e.what();
    } catch (...) {
        LOG(WARNING) << "Ignoring invalid config file: unknown error";
    }

    return config;
}

void apply_runtime_config(MainWindow& main_window, const AppConfig& config) {
    if (!config.api_key.empty() && main_window.ai_chat())
        main_window.ai_chat()->SetApiKey(config.api_key);

    set_scene_lighting_enabled(config.scene_lighting_enabled);
    apply_default_scene_material_settings();

    if (config.background_color[0] >= 0.0f && main_window.viewer()) {
        main_window.viewer()->set_background_color(
            easy3d::vec4(config.background_color[0], config.background_color[1],
                         config.background_color[2], config.background_color[3]));
    }
    if (config.has_lighting) {
        easy3d::setting::light_position =
            easy3d::vec4(config.light_position[0], config.light_position[1],
                         config.light_position[2], config.light_position[3]);
        easy3d::setting::material_ambient =
            easy3d::vec4(config.material_ambient[0], config.material_ambient[1],
                         config.material_ambient[2], config.material_ambient[3]);
        easy3d::setting::material_specular =
            easy3d::vec4(config.material_specular[0], config.material_specular[1],
                         config.material_specular[2], config.material_specular[3]);
        easy3d::setting::material_shininess = config.material_shininess;
    }
}

void save_app_config(GLFWwindow* window, MainWindow* main_window) {
    const std::string config_path =
        platform::app_config_path(product_identity::kConfigFileName);

    json data;
    int x, y, width, height;
    glfwGetWindowPos(window, &x, &y);
    glfwGetWindowSize(window, &width, &height);
    data["window_x"] = x;
    data["window_y"] = y;
    data["window_w"] = width;
    data["window_h"] = height;
    data["ui_scale"] = UIScale::instance().scale();
    data["ai_language"] = ai_lang::to_id(ai_lang::current());
    data["scene_lighting_enabled"] = scene_lighting_enabled();
    data["scene_material_version"] = kSceneMaterialVersion;

    if (main_window && main_window->ai_chat() && main_window->ai_chat()->HasApiKey())
        data["api_key"] = main_window->ai_chat()->GetApiKey();

    if (main_window && main_window->viewer()) {
        const auto& color = main_window->viewer()->background_color();
        data["background_color"] = {color[0], color[1], color[2], color[3]};
    }
    data["light_position"] = {easy3d::setting::light_position[0],
                              easy3d::setting::light_position[1],
                              easy3d::setting::light_position[2],
                              easy3d::setting::light_position[3]};
    data["material_ambient"] = {easy3d::setting::material_ambient[0],
                                easy3d::setting::material_ambient[1],
                                easy3d::setting::material_ambient[2],
                                easy3d::setting::material_ambient[3]};
    data["material_specular"] = {easy3d::setting::material_specular[0],
                                 easy3d::setting::material_specular[1],
                                 easy3d::setting::material_specular[2],
                                 easy3d::setting::material_specular[3]};
    data["material_shininess"] = easy3d::setting::material_shininess;

    if (config_path.empty()) {
        LOG(WARNING) << "Cannot resolve application config path.";
        return;
    }

    std::ofstream file(config_path);
    if (file.is_open())
        file << data.dump(2);
    else
        LOG(WARNING) << "Cannot write application config: " << config_path;
}

} // namespace claw3d
