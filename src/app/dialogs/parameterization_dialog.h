// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PARAMETERIZATION_DIALOG_H
#define CLAW3D_PARAMETERIZATION_DIALOG_H

/// State and render entry point for mesh parameterization controls.

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "services/jobs/cgal/parameterization_job.h"

class ViewportCanvas;

// Staged reveal animation phases for LSCM process visualization.
enum ParamRevealPhase {
    PARAM_REVEAL_Idle = 0,
    PARAM_REVEAL_Boundary,   // 3D boundary highlighted
    PARAM_REVEAL_UVOutline,  // UV boundary polygon drawn
    PARAM_REVEAL_Filling,    // interior triangles filling in
    PARAM_REVEAL_Edges,      // all UV edges visible
    PARAM_REVEAL_Distortion, // distortion heatmap active
    PARAM_REVEAL_Complete    // picking enabled
};

// One closed-mesh seam path between two user-picked vertices.
struct ParamSeamUIPath {
    int start_vertex = -1;
    int end_vertex = -1;
    std::vector<int> vertex_ids;       // full vertex sequence start..end
    double length = 0.0;
};

struct ParameterizationState {
    ParameterizationState() = default;
    ParameterizationState(const ParameterizationState& other) { *this = other; }
    ParameterizationState& operator=(const ParameterizationState& other) {
        runner             = other.runner;
        last_result_valid  = other.last_result_valid;
        last_result        = other.last_result;
        last_error         = other.last_error;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        close_requested    = other.close_requested;
        algorithm          = other.algorithm;
        arap_iterations    = other.arap_iterations;
        arap_tolerance     = other.arap_tolerance;
        arap_lambda        = other.arap_lambda;
        show_process       = other.show_process;
        process_speed      = other.process_speed;
        arap_playback_frame = other.arap_playback_frame;
        arap_playback_face_uvs = other.arap_playback_face_uvs;
        arap_live_active   = other.arap_live_active;
        arap_live_iteration = other.arap_live_iteration;
        arap_live_energy   = other.arap_live_energy;
        arap_live_snapshot_count = other.arap_live_snapshot_count;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        // Staged reveal
        reveal_phase       = other.reveal_phase;
        reveal_progress    = other.reveal_progress;
        reveal_start_time  = other.reveal_start_time;
        reveal_sorted_faces= other.reveal_sorted_faces;
        reveal_face_count  = other.reveal_face_count;
        // UV view
        uv_pan_x  = other.uv_pan_x;   uv_pan_y  = other.uv_pan_y;
        uv_zoom   = other.uv_zoom;
        uv_picked_face = other.uv_picked_face;
        uv_picked_u = other.uv_picked_u; uv_picked_v = other.uv_picked_v;
        uv_hovered_face = other.uv_hovered_face;
        uv_need_fit     = other.uv_need_fit;
        uv_show_heatmap  = other.uv_show_heatmap;
        uv_show_edges    = other.uv_show_edges;
        uv_show_boundary = other.uv_show_boundary;
        // 3D / UV correspondence picking
        pick_mode        = other.pick_mode;
        owns_viewport_input_lock = false;
        picked_3d_face   = other.picked_3d_face;
        picked_3d_x = other.picked_3d_x;
        picked_3d_y = other.picked_3d_y;
        picked_3d_z = other.picked_3d_z;
        std::memcpy(pick_status, other.pick_status, sizeof(pick_status));
        // Closed-mesh seam workflow
        seam_pick_mode      = other.seam_pick_mode;
        seam_pending_start  = other.seam_pending_start;
        seam_pending_end    = other.seam_pending_end;
        seam_paths          = other.seam_paths;
        show_seams          = other.show_seams;
        std::memcpy(seam_status, other.seam_status, sizeof(seam_status));
        return *this;
    }

    // Runner
    claw3d::services::ParameterizationJobHandle runner;
    bool   last_result_valid = false;
    PARAM_Result last_result{};
    std::string last_error;
    std::string last_input_metadata_prompt;

    bool   close_requested = false;
    std::atomic<bool> final_result_ready{true};

    // Algorithm (ARAP)
    int    algorithm       = PARAM_ALGO_LSCM;
    int    arap_iterations = 50;
    double arap_tolerance  = 1e-6;
    double arap_lambda     = 1000.0;
    bool   show_process    = true;
    int    process_speed   = 0; // 0=normal, 1=slow

    // Staged reveal animation
    int   reveal_phase = PARAM_REVEAL_Idle;
    float reveal_progress = 0.0f; // 0-1 within current phase
    double reveal_start_time = 0.0;
    std::vector<int> reveal_sorted_faces; // face indices in reveal order
    int   reveal_face_count = 0; // how many faces currently visible in filling phase

    // UV view state
    float uv_pan_x  = 0.0f;
    float uv_pan_y  = 0.0f;
    float uv_zoom   = 1.0f;
    int   uv_picked_face = -1;
    float uv_picked_u = 0.0f, uv_picked_v = 0.0f;
    int   uv_hovered_face = -1;
    bool  uv_need_fit     = true;
    bool  uv_show_heatmap  = true;
    bool  uv_show_edges    = false;
    bool  uv_show_boundary = true;

    // 3D / UV correspondence picking.
    // pick_mode: 0=off, 1=pick from 3D viewport (one-shot).
    int   pick_mode = 0;
    bool  owns_viewport_input_lock = false;
    int   picked_3d_face = -1;
    float picked_3d_x = 0.0f, picked_3d_y = 0.0f, picked_3d_z = 0.0f;
    char  pick_status[128] = "";

    // ARAP live snapshot currently displayed in the UV view.
    int   arap_playback_frame = -1; // -1 = show final result
    std::vector<PARAM_FaceUV> arap_playback_face_uvs;
    bool  arap_live_active = false;
    int   arap_live_iteration = -1;
    double arap_live_energy = 0.0;
    int   arap_live_snapshot_count = 0;

    // Closed-mesh seam workflow.
    // seam_pick_mode: 0=off, 1=pick start vertex, 2=pick end vertex (one-shot).
    int   seam_pick_mode      = 0;
    int   seam_pending_start  = -1;
    int   seam_pending_end    = -1;
    std::vector<ParamSeamUIPath> seam_paths;
    bool  show_seams          = true;
    char  seam_status[160]    = "";
};

void renderDialogParameterization(ViewportCanvas* viewer,
                                   ParameterizationState& s, bool& open);

#endif // CLAW3D_PARAMETERIZATION_DIALOG_H
