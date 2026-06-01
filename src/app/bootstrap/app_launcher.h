// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_LAUNCHER_H
#define CLAW3D_APP_LAUNCHER_H

/// Application startup entry points that wire configuration, windowing, and UI.

#include <string>
#include <vector>

struct GLFWwindow;
class MainWindow;

namespace claw3d {

class AppLauncher {
public:
    int run(int argc, char** argv);

private:
    static void drop_callback(GLFWwindow* window, int count, const char** paths);

    GLFWwindow* create_window();
    void on_drop(int count, const char** paths);
    void process_dropped_files(MainWindow& main_window);
    void run_main_loop(GLFWwindow* window, MainWindow& main_window);

    std::vector<std::string> dropped_files_;
};

} // namespace claw3d

#endif // CLAW3D_APP_LAUNCHER_H
