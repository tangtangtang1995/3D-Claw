// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_RESOURCE_PATHS_H
#define CLAW3D_SERVICES_RESOURCE_PATHS_H

/// Easy3D resource location and initialization helpers.

#include <string>

namespace claw3d::resources {

/// Initializes Easy3D's resource directory search path once at startup.
void initialize_easy3d_resources();

/// Returns the resolved Easy3D resource root.
std::string easy3d_resource_directory();
/// Returns an absolute path under the resolved Easy3D resource root.
std::string easy3d_resource_path(const std::string& relative_path);
/// Returns the resolved Easy3D sample-data directory.
std::string easy3d_data_directory();

} // namespace claw3d::resources

#endif // CLAW3D_SERVICES_RESOURCE_PATHS_H
