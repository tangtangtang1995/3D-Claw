// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MEASUREMENT_DIALOG_H
#define CLAW3D_MEASUREMENT_DIALOG_H

/// State and render entry point for interactive measurement tools.

#include <string>
#include <vector>
#include <easy3d/core/types.h>

class ViewportCanvas;
class MainWindow;

enum class MeasureType {
    Distance,
    Polyline,
    Angle,
    MtBBox,
    MtSurfaceArea,
    MtVolume,
};

struct MeasurePoint {
    easy3d::vec3 pos;
    char label[16];
};

// One finalized measurement -- e.g. distance between P0 and P1, the angle
// at P1 in P0-P1-P2, or a complete polyline.
struct MeasurementResult {
    MeasureType type = MeasureType::Distance;
    std::vector<MeasurePoint> points;
    float value = 0.0f;
    char label[80] = "";
};

struct MeasurementState {
    MeasureType type = MeasureType::Distance;
    // Points currently being collected (the in-progress measurement).
    // For Distance/Angle this auto-finalizes into `history` once full;
    // for Polyline it stays in-progress until the user clicks Finish.
    std::vector<MeasurePoint> points;
    // All previously finalized measurements in this session, oldest first.
    std::vector<MeasurementResult> history;
    bool picking = false;
    bool overlay_dirty = false;
    // When true: a click on a SurfaceMesh face snaps to that face's nearest
    // vertex (no hit-radius constraint, always picks the closest of the 3
    // -- pickable from anywhere inside the face).
    // When false: a click records the exact ray-surface intersection point
    // on the face (free surface point, useful for measuring distances
    // between non-vertex locations).
    // PointCloud picks always go to the nearest actual point regardless.
    bool snap_to_vertex = true;
    int next_label = 0;     // monotonic across the session (P0, P1, P2, ...)

    void reset();             // wipe both in-progress and history
    void clear_history();     // keep in-progress, drop history
    int expected_points() const;
    void add_point(const easy3d::vec3& pos);
    // Finalize whatever is in `points` right now (used by Polyline's
    // Finish button and internally by add_point when the group fills up).
    void finalize_current();
};

void renderDialogMeasurement(ViewportCanvas* viewer, MainWindow* win,
                             MeasurementState& s, bool& open);

#endif // CLAW3D_MEASUREMENT_DIALOG_H
