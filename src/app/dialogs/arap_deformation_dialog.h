// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ARAP_DEFORMATION_DIALOG_H
#define CLAW3D_ARAP_DEFORMATION_DIALOG_H

/// State and render entry point for ARAP deformation setup and execution.

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "services/jobs/cgal/arap_deformation_job.h"

class ViewportCanvas;

struct ARAPDeformationState {
    ARAPDeformationState() = default;
    ARAPDeformationState(const ARAPDeformationState& other) { *this = other; }
    ARAPDeformationState& operator=(const ARAPDeformationState& other) {
        roi_vertices      = other.roi_vertices;
        control_vertices  = other.control_vertices;
        control_group_ids = other.control_group_ids;
        roi_k_ring        = other.roi_k_ring;
        roi_seed          = other.roi_seed;
        active_group      = other.active_group;
        mode              = other.mode;
        iterations        = other.iterations;
        tolerance         = other.tolerance;
        tx = other.tx; ty = other.ty; tz = other.tz;
        rx_deg = other.rx_deg; ry_deg = other.ry_deg; rz_deg = other.rz_deg;
        runner            = other.runner;
        last_result_valid = other.last_result_valid;
        last_result       = other.last_result;
        last_error        = other.last_error;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        pick_mode         = other.pick_mode;
        owns_viewport_input_lock = false;
        std::memcpy(pick_status, other.pick_status, sizeof(pick_status));
        close_requested   = other.close_requested;
        live_preview      = other.live_preview;
        preview_speed     = other.preview_speed;
        preview_steps     = other.preview_steps;
        last_snap_gen     = other.last_snap_gen;
        settling          = other.settling;
        settle_started_at = other.settle_started_at;
        settle_ms         = other.settle_ms;
        drag_active       = other.drag_active;
        drag_start_mx     = other.drag_start_mx;
        drag_start_my     = other.drag_start_my;
        drag_start_tx     = other.drag_start_tx;
        drag_start_ty     = other.drag_start_ty;
        drag_start_tz     = other.drag_start_tz;
        drag_cam_rx       = other.drag_cam_rx;
        drag_cam_ry       = other.drag_cam_ry;
        drag_cam_rz       = other.drag_cam_rz;
        drag_cam_ux       = other.drag_cam_ux;
        drag_cam_uy       = other.drag_cam_uy;
        drag_cam_uz       = other.drag_cam_uz;
        drag_sensitivity  = other.drag_sensitivity;
        drag_mode         = other.drag_mode;
        drag_start_rx_deg = other.drag_start_rx_deg;
        drag_start_ry_deg = other.drag_start_ry_deg;
        drag_start_rz_deg = other.drag_start_rz_deg;
        drag_rot_sensitivity_rad_per_px = other.drag_rot_sensitivity_rad_per_px;
        auto_run_pending  = other.auto_run_pending;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }
    // Selection
    std::vector<int> roi_vertices;
    std::vector<int> control_vertices;
    std::vector<int> control_group_ids;
    int roi_k_ring    = 8;
    int roi_seed      = -1;
    int active_group  = 0;

    // Algorithm params
    int    mode       = ARAP_MODE_SpokesAndRims;
    int    iterations = 10;
    double tolerance  = 1e-4;

    // Transform for the active group
    double tx     = 0.0;
    double ty     = 0.0;
    double tz     = 0.0;
    double rx_deg = 0.0;
    double ry_deg = 0.0;
    double rz_deg = 0.0;

    // Runner and results
    claw3d::services::ArapDeformationJobHandle runner;
    bool   last_result_valid = false;
    ARAP_Result last_result{};
    std::string last_error;

    // Picking
    int  pick_mode  = 0;  // 0=None, 1=ROI Seed, 2=Control, 3=Erase
    bool owns_viewport_input_lock = false;
    char pick_status[128] = {};

    // AI
    std::string last_input_metadata_prompt;

    bool   close_requested = false;
    // Live preview
    bool   live_preview      = false;
    int    preview_speed     = ARAP_PREVIEW_Normal;
    int    preview_steps     = 16;
    int    last_snap_gen     = -1;
    bool   settling          = false;
    double settle_started_at = 0.0;
    double settle_ms         = 1500.0;
    // Drag-handle bookkeeping. Screen-space drag delta -> world delta
    // along camera right + up axes (we capture them at mouse-down so the
    // mapping stays consistent through the drag even if the camera rotates).
    bool   drag_active     = false;
    float  drag_start_mx   = 0.0f;
    float  drag_start_my   = 0.0f;
    double drag_start_tx   = 0.0;
    double drag_start_ty   = 0.0;
    double drag_start_tz   = 0.0;
    double drag_cam_rx     = 1.0;  // camera right vector
    double drag_cam_ry     = 0.0;
    double drag_cam_rz     = 0.0;
    double drag_cam_ux     = 0.0;  // camera up vector
    double drag_cam_uy     = 1.0;
    double drag_cam_uz     = 0.0;
    double drag_sensitivity = 1e-3; // world units per pixel (translate)
    // Drag interaction mode. 0 = translate (Shift+drag), 1 = rotate (Ctrl+drag).
    int    drag_mode       = 0;
    // Baseline rotation in ZYX Euler degrees, captured at drag-start.
    double drag_start_rx_deg = 0.0;
    double drag_start_ry_deg = 0.0;
    double drag_start_rz_deg = 0.0;
    double drag_rot_sensitivity_rad_per_px = 0.0; // radians per pixel
    // Polish: when set, the Run handler triggers itself on the next
    // frame as if the user clicked Run. Drag release sets this so the
    // mesh deforms automatically when the user lets go of the mouse,
    // matching the CGAL Lab interaction.
    bool   auto_run_pending = false;
    std::atomic<bool> final_result_ready{true};
};

void renderDialogARAPDeformation(ViewportCanvas* viewer,
                                  ARAPDeformationState& s, bool& open);

#endif // CLAW3D_ARAP_DEFORMATION_DIALOG_H
