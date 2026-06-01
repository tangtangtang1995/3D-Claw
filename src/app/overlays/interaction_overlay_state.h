// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_INTERACTION_OVERLAY_STATE_H
#define CLAW3D_INTERACTION_OVERLAY_STATE_H

/// Shared state for interactive viewport overlays such as picks and handles.

namespace easy3d {
class Graph;
class Model;
class PointCloud;
class SurfaceMesh;
}

struct InteractionOverlayState {
    bool selection_visible = true;
    easy3d::SurfaceMesh* selection_faces = nullptr;
    easy3d::Graph* selection_vertices = nullptr;
    easy3d::PointCloud* selection_points = nullptr;
    easy3d::Model* selection_source = nullptr;

    easy3d::Graph* measurement = nullptr;
    easy3d::Graph* crop_gizmo = nullptr;
    easy3d::Graph* align_gizmo = nullptr;
    int crop_artifact_serial = 1;

    easy3d::Graph* geodesic_sources = nullptr;
    easy3d::Graph* geodesic_target = nullptr;
    easy3d::Graph* geodesic_front_path = nullptr;
    easy3d::Graph* geodesic_exact_path = nullptr;
};

#endif // CLAW3D_INTERACTION_OVERLAY_STATE_H
