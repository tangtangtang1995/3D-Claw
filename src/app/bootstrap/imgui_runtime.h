// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_IMGUI_RUNTIME_H
#define CLAW3D_IMGUI_RUNTIME_H

/// ImGui context, font, and renderer backend lifecycle for the desktop app.

struct GLFWwindow;

namespace claw3d {

bool initialize_imgui_runtime(GLFWwindow* window);
void shutdown_imgui_runtime();
void begin_imgui_frame();
void end_imgui_frame(GLFWwindow* window);

} // namespace claw3d

#endif // CLAW3D_IMGUI_RUNTIME_H
