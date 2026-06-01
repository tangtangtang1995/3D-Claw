// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_CROP_DIALOG_H
#define CLAW3D_CROP_DIALOG_H

/// State and render entry point for interactive model cropping.

#include <string>

#include <easy3d/core/types.h>

class ViewportCanvas;
class MainWindow;

enum class CropMode : int {
    Box,
    PlaneXY,
    PlaneYZ,
    PlaneXZ,
    PlaneCustom,
    Selection,
};

struct CropState {
    CropMode mode = CropMode::Box;

    // Box (stored as center + half-extents for gizmo; min/max are derived)
    float box_center[3] = {0, 0, 0};
    float box_half[3]   = {1, 1, 1};
    float box_rotation_deg[3] = {0, 0, 0}; // XYZ Euler, R = Rz * Ry * Rx
    bool  box_initialized = false;
    bool  keep_inside = true;

    // Plane
    float plane_normal[3] = {0, 0, 1};
    float plane_offset = 0;  // signed distance from origin along normal

    bool  keep_positive = true;
    std::string last_result_summary;
    bool last_result_valid = false;

    // Gizmo interaction.
    // 0..5: face arrows (+X, -X, +Y, -Y, +Z, -Z), 6..8: rotation rings (X/Y/Z).
    int   gizmo_axis = -1;
    bool  gizmo_dragging = false;
    bool  gizmo_dirty = false;
    float gizmo_start_bound = 0; // value of the bound (min or max) at drag start
    float gizmo_start_opposite = 0; // the opposite bound on the same axis
    float gizmo_start_center_val = 0; // center on this axis
    float gizmo_start_half_val = 0;   // half-extent on this axis
    float gizmo_start_axis_param = 0; // closest-point parameter along picked axis
    float gizmo_start_mouse[2] = {0, 0};
    float gizmo_start_center[3] = {0, 0, 0};
    float gizmo_start_half[3] = {1, 1, 1};
    float gizmo_start_rotation_deg[3] = {0, 0, 0};

    // Helpers
    void box_min(float out[3]) const;
    void box_max(float out[3]) const;
    void box_basis(easy3d::vec3 out[3]) const;
    easy3d::vec3 box_local_to_world(const easy3d::vec3& p) const;
    easy3d::vec3 box_world_to_local(const easy3d::vec3& p) const;
    void box_corners(easy3d::vec3 out[8]) const;
    easy3d::Box3 box_world_aabb() const;
    void set_box_from_bb(const easy3d::Box3& bb);
    void set_plane_from_bbox(const easy3d::Box3& bb);
};

void renderDialogCrop(ViewportCanvas* viewer, MainWindow* win,
                      CropState& s, bool& open);

#endif // CLAW3D_CROP_DIALOG_H
