// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "platform/clipboard.h"

#include <GLFW/glfw3.h>

namespace claw3d::app {

void set_clipboard_text(const std::string& text) {
    glfwSetClipboardString(nullptr, text.c_str());
}

std::string clipboard_text() {
    const char* text = glfwGetClipboardString(nullptr);
    return text ? std::string(text) : std::string();
}

} // namespace claw3d::app
