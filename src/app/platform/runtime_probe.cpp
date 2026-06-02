// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "platform/runtime_probe.h"

#include <easy3d/renderer/opengl.h>

namespace claw3d::app {

GraphicsRuntimeInfo probe_graphics_runtime() {
    GraphicsRuntimeInfo info;
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    if (renderer)
        info.renderer = reinterpret_cast<const char*>(renderer);
    if (version)
        info.version = reinterpret_cast<const char*>(version);
    return info;
}

} // namespace claw3d::app
