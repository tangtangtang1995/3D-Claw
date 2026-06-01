// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_WINDOW_HELPERS_H
#define CLAW3D_WINDOW_HELPERS_H

/// Small window-layer helpers that keep main-window code focused on state flow.

// Private utilities shared between main_window.cpp and the split
// helper TUs (file_actions.cpp, ai_chat_panel.cpp,
// selection_queries.cpp, *_overlays.cpp, etc.).
// These are NOT part of the public MainWindow interface  - they are
// implementation details that happened to be reused after we split the
// monolithic 6700-line main_window.cpp.

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/types.h>

namespace easy3d { class Model; }
class MainWindow;

// Apply a baked texture (from `m:texture_diffuse` model property + UV
// halfedge/vertex coords) onto the mesh's "faces" drawable, if present.
// Returns true if a texture was actually attached.
bool apply_surface_mesh_texture(easy3d::Model* model);

easy3d::Model* resolve_current_algorithm_source(MainWindow* win);
void mark_algorithm_done(MainWindow* win);

// Helper that snapshots and re-creates vertex / face / halfedge
// attributes (normals, colors, texcoords, model texture metadata) from
// a source SurfaceMesh on a destination one. Used by extract_selection
// to carry attributes over to the new mesh.
struct SurfaceMeshAttributeCopier {
    easy3d::SurfaceMesh* src = nullptr;
    easy3d::SurfaceMesh* dst = nullptr;

    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3>   snormal;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3>   scolor;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec2>   stexcoord;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3>     fnormal;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3>     fcolor;
    easy3d::SurfaceMesh::HalfedgeProperty<easy3d::vec2> htexcoord;

    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3>   dnormal;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3>   dcolor;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec2>   dtexcoord;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3>     dfnormal;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3>     dfcolor;
    easy3d::SurfaceMesh::HalfedgeProperty<easy3d::vec2> dhtexcoord;

    SurfaceMeshAttributeCopier(easy3d::SurfaceMesh* s, easy3d::SurfaceMesh* d);
    void copy_vertex(easy3d::SurfaceMesh::Vertex sv,
                     easy3d::SurfaceMesh::Vertex dv);
    void copy_face(easy3d::SurfaceMesh::Face sf,
                   easy3d::SurfaceMesh::Face df);
};

#endif // CLAW3D_WINDOW_HELPERS_H
