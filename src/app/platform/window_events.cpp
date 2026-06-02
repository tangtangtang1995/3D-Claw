// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "platform/window_events.h"

#include <GLFW/glfw3.h>

namespace claw3d::app {

void wake_event_loop() {
    glfwPostEmptyEvent();
}

void request_window_close() {
    if (auto* window = glfwGetCurrentContext())
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

} // namespace claw3d::app
