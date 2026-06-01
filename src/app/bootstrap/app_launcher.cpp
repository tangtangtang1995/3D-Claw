// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "bootstrap/app_launcher.h"

#include "ai/ai_context.h"
#include "bootstrap/app_config.h"
#include "bootstrap/imgui_runtime.h"
#include "services/resources/resource_paths.h"
#include "window/main_window.h"
#include "product_identity.h"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <easy3d/util/initializer.h>

#include <utility>
#include <cstdlib>

namespace claw3d {
namespace {

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 720;
constexpr double kLoadingWaitSeconds = 0.033;
constexpr double kIdleWaitSeconds = 0.1;

} // namespace


int AppLauncher::run(int argc, char** argv) {
    easy3d::initialize(false, true, true);
    resources::initialize_easy3d_resources();

    if (!glfwInit())
        return EXIT_FAILURE;

    GLFWwindow* window = create_window();
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetDropCallback(window, AppLauncher::drop_callback);

    AppConfig config = load_app_config(window);

    if (!initialize_imgui_runtime(window)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    AIContext::instance().probe_runtime();

    {
        MainWindow main_window;
        apply_runtime_config(main_window, config);

        if (argc > 1) {
            main_window.viewer()->clear_scene();
            main_window.viewer()->add_model(std::string(argv[1]));
        }

        run_main_loop(window, main_window);
        save_app_config(window, &main_window);
    }

    shutdown_imgui_runtime();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

void AppLauncher::drop_callback(GLFWwindow* window, int count, const char** paths) {
    auto* launcher = static_cast<AppLauncher*>(glfwGetWindowUserPointer(window));
    if (launcher)
        launcher->on_drop(count, paths);
}

GLFWwindow* AppLauncher::create_window() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        kDefaultWindowWidth, kDefaultWindowHeight,
        product_identity::kDisplayName, nullptr, nullptr);
    if (!window)
        return nullptr;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    return window;
}

void AppLauncher::on_drop(int count, const char** paths) {
    for (int i = 0; i < count; ++i)
        dropped_files_.push_back(paths[i]);
}

void AppLauncher::process_dropped_files(MainWindow& main_window) {
    if (dropped_files_.empty())
        return;

    std::vector<std::string> files;
    files.swap(dropped_files_);
    main_window.load_files_async(std::move(files));
}

void AppLauncher::run_main_loop(GLFWwindow* window, MainWindow& main_window) {
    while (!glfwWindowShouldClose(window)) {
        process_dropped_files(main_window);

        if (main_window.viewer()->is_dirty() || ImGui::IsAnyItemActive()) {
            glfwPollEvents();
        } else if (main_window.is_loading()) {
            glfwWaitEventsTimeout(kLoadingWaitSeconds);
        } else {
            glfwWaitEventsTimeout(kIdleWaitSeconds);
        }

        begin_imgui_frame();
        main_window.render();
        end_imgui_frame(window);
    }
}

} // namespace claw3d
