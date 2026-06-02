// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_PLATFORM_RUNTIME_PROBE_H
#define CLAW3D_APP_PLATFORM_RUNTIME_PROBE_H

/// Graphics-runtime probes that require a current OpenGL context.

#include <string>

namespace claw3d::app {

struct GraphicsRuntimeInfo {
    std::string renderer;
    std::string version;
};

GraphicsRuntimeInfo probe_graphics_runtime();

} // namespace claw3d::app

#endif // CLAW3D_APP_PLATFORM_RUNTIME_PROBE_H
