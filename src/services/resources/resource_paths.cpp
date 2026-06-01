// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/resources/resource_paths.h"

#include "services/platform/platform_paths.h"

#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/resource.h>

#include <cstdlib>
#include <string>

#ifndef CLAW3D_EASY3D_RESOURCE_DIR
#define CLAW3D_EASY3D_RESOURCE_DIR ""
#endif

namespace claw3d::resources {
namespace {

bool is_valid_easy3d_resource_dir(const std::string& dir) {
    return easy3d::file_system::is_directory(dir) &&
        easy3d::file_system::is_directory(dir + "/shaders") &&
        easy3d::file_system::is_directory(dir + "/fonts") &&
        easy3d::file_system::is_directory(dir + "/colormaps") &&
        easy3d::file_system::is_directory(dir + "/textures");
}

std::string resolve_easy3d_resource_directory() {
    const char* env = std::getenv("CLAW3D_EASY3D_RESOURCE_DIR");
    if (env && is_valid_easy3d_resource_dir(env)) {
        easy3d::resource::initialize(env);
        return easy3d::file_system::convert_to_native_style(env);
    }

    const std::string executable_dir = platform::executable_directory();
    if (!executable_dir.empty()) {
        const std::string portable_dir = executable_dir + "/resources";
        if (is_valid_easy3d_resource_dir(portable_dir)) {
            easy3d::resource::initialize(portable_dir);
            return easy3d::file_system::convert_to_native_style(portable_dir);
        }
    }

    const std::string configured = CLAW3D_EASY3D_RESOURCE_DIR;
    if (!configured.empty() && is_valid_easy3d_resource_dir(configured)) {
        easy3d::resource::initialize(configured);
        return easy3d::file_system::convert_to_native_style(configured);
    }

    const std::string easy3d_dir = easy3d::resource::directory();
    if (is_valid_easy3d_resource_dir(easy3d_dir))
        return easy3d_dir;

    LOG_N_TIMES(1, WARNING)
        << "3D Claw could not validate Easy3D resources. Set "
           "CLAW3D_EASY3D_RESOURCE_DIR or pass -DCLAW3D_EASY3D_RESOURCE_DIR "
           "to a directory containing shaders, fonts, colormaps, and textures.";
    return easy3d_dir;
}

} // namespace

void initialize_easy3d_resources() {
    (void)easy3d_resource_directory();
}

std::string easy3d_resource_directory() {
    static const std::string dir = resolve_easy3d_resource_directory();
    return dir;
}

std::string easy3d_resource_path(const std::string& relative_path) {
    if (relative_path.empty())
        return easy3d_resource_directory();
    if (relative_path[0] == '/' || relative_path[0] == '\\')
        return easy3d_resource_directory() + relative_path;
    return easy3d_resource_directory() + "/" + relative_path;
}

std::string easy3d_data_directory() {
    return easy3d_resource_path("data");
}

} // namespace claw3d::resources
