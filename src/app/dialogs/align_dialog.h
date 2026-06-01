// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALIGN_DIALOG_H
#define CLAW3D_ALIGN_DIALOG_H

/// State and render entry point for source/target alignment tools.

#include <string>

#include <easy3d/core/types.h>

namespace easy3d { class Model; }

class ViewportCanvas;
class MainWindow;

enum class AlignTab : int {
    Transform,
    PointPair,
    ICP,
};

struct AlignState {
    AlignTab tab = AlignTab::Transform;

    // ---- Transform ----
    float tx = 0.f, ty = 0.f, tz = 0.f;          // translation
    float rx = 0.f, ry = 0.f, rz = 0.f;          // rotation (Euler degrees, XYZ order)
    float sx = 1.f, sy = 1.f, sz = 1.f;          // scale
    bool  uniform_scale = true;                  // bind sy/sz to sx
    bool  transform_show_gizmo = true;           // 3D in-viewport gizmo
    std::string transform_status;

    // Transform gizmo runtime state driven by viewport drag.
    // Translation arrows and rotation rings around the target model's
    // bbox center; scale is intentionally numeric-only.
    bool gizmo_dragging = false;
    int  gizmo_axis = -1;                  // 0-5: trans (+X,-X,+Y,-Y,+Z,-Z); 6-8: rot rings X/Y/Z
    float gizmo_start_mouse[2] = {0, 0};
    float gizmo_start_t[3]     = {0, 0, 0};   // tx/ty/tz at press
    float gizmo_start_r[3]     = {0, 0, 0};   // rx/ry/rz at press
    float gizmo_start_axis_param = 0.f;
    easy3d::vec3 gizmo_start_center{0, 0, 0}; // bbox.center() + start translation
    easy3d::Model* gizmo_target = nullptr;

    // Live preview state: we directly transform vertices each frame during
    // gizmo drag and numeric input, then restore on dialog close/Reset.
    easy3d::Model* preview_model = nullptr;
    std::vector<easy3d::vec3> preview_original_pts; // snapshot before first preview
    int preview_applied_rev = 0; // incremented each time preview is applied

    // ---- Point-pair ----
    // The two endpoints are model pointers (lifetime-managed by the viewer);
    // we don't keep the picked indices snapshot here -- the dialog re-reads
    // SelectionManager every Apply so it always sees the latest user picks.
    easy3d::Model* pp_source_model = nullptr;
    easy3d::Model* pp_target_model = nullptr;
    std::string pp_status;
    float pp_last_rms = -1.f;
    int   pp_last_pair_count = 0;

    // ---- ICP ----
    easy3d::Model* icp_source_model = nullptr;
    easy3d::Model* icp_target_model = nullptr;
    int   icp_max_iter = 50;
    int   icp_sample_count = 5000;     // subsample the source for speed
    float icp_outlier_factor = 3.0f;   // reject correspondences > k * median distance
    float icp_tolerance = 1e-5f;       // converge when RMS change is below
    std::string icp_status;
    float icp_last_rms = -1.f;
    int   icp_last_iter = 0;
};

void renderDialogAlign(ViewportCanvas* viewer, MainWindow* win,
                       AlignState& s, bool& open);

#endif // CLAW3D_ALIGN_DIALOG_H
