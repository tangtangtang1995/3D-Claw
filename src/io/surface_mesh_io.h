// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_IO_SURFACE_MESH_IO_H
#define CLAW3D_IO_SURFACE_MESH_IO_H

/// Surface mesh load/save facade used by app and services.

#include <string>

namespace easy3d {
class SurfaceMesh;
}

namespace claw3d::io {

/// Loads a supported mesh file and returns an owned Easy3D mesh, or nullptr.
easy3d::SurfaceMesh* load_surface_mesh(const std::string& file_name);

/// Saves an Easy3D surface mesh using the format implied by file_name.
bool save_surface_mesh(const std::string& file_name,
                       const easy3d::SurfaceMesh* mesh);

/// Returns true when file_name has a supported surface-mesh extension.
bool is_surface_mesh_file(const std::string& file_name);

/// Loads glTF/GLB mesh geometry into an existing Easy3D surface mesh.
bool load_gltf_surface_mesh(const std::string& file_name,
                            easy3d::SurfaceMesh* mesh);

} // namespace claw3d::io

#endif // CLAW3D_IO_SURFACE_MESH_IO_H
