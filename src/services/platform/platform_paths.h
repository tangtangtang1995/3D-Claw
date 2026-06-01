// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_PLATFORM_PATHS_H
#define CLAW3D_SERVICES_PLATFORM_PATHS_H

/// Cross-platform filesystem locations used by app configuration and caches.

#include <string>

namespace claw3d::platform {

/// Returns the directory containing the running executable.
std::string executable_directory();
/// Returns the writable user config directory, or an empty string on failure.
std::string user_config_directory();
/// Returns the writable user cache directory, or an empty string on failure.
std::string user_cache_directory();
/// Returns a writable temporary directory.
std::string temporary_directory();
/// Returns the cache directory used for downloaded 3D generation results.
std::string generation_cache_directory();
/// Returns the persistent output directory for generated 3D models.
std::string generated_models_directory();
/// Returns the full path for a config file stored in the user config dir.
std::string app_config_path(const std::string& file_name);
/// Returns a platform CJK font path suitable for ImGui font loading.
std::string locate_cjk_font();
/// Opens a directory in the platform file manager.
bool open_directory(const std::string& path);

} // namespace claw3d::platform

#endif // CLAW3D_SERVICES_PLATFORM_PATHS_H
