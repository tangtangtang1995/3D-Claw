// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ANIMATION_DIALOG_H
#define CLAW3D_ANIMATION_DIALOG_H

/// State and render entry point for camera animation playback and export.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <easy3d/renderer/frame.h>

class ViewportCanvas;
class MainWindow;

// Mirrors WalkThrough::Status
enum class AnimationMode : int {
    Free,
    Walking,
    RotateAround,
};

struct AnimationState {
    AnimationMode mode = AnimationMode::Free;

    // Tracks first frame after dialog opens so we can apply defaults
    // (Show Path / Show Cameras = true) without overriding user toggles later.
    bool first_render = true;

    // Interpolation
    int interp_method = 0;   // 0=Spline Interpolation, 1=Spline Fitting
    float interp_speed = 1.0f;
    int frame_rate = 30;

    // Walking mode
    bool follow_up = true;
    float height_factor = 0.2f;
    float forward_factor = 1.8f;

    // Rotate-around-axis mode
    float zoom_out_factor = 0.0f;
    float vert_offset_factor = 0.0f;
    float pitch_angle = 0.0f;
    int keyframe_samples = 10;
    int num_loops = 2;

    // Output
    char output_file[512] = "";

    // ---- Recording state machine ----
    // Record snapshots one frame per dialog tick so the UI stays responsive.
    // All these are owned by the dialog; cleared on completion / cancel /
    // dialog close.
    bool                       rec_active = false;
    bool                       rec_cancel = false;
    int                        rec_frame = 0;     // next index to write
    int                        rec_ok = 0;
    int                        rec_fail = 0;
    int                        rec_w = 0;
    int                        rec_h = 0;
    bool                       rec_path_was_visible = false;
    bool                       rec_cams_was_visible = false;
    std::string                rec_stem;          // path without extension
    std::string                rec_ext;           // extension (no dot)
    std::vector<easy3d::Frame> rec_frames;        // copy of KFI interpolated path

    AnimationState() { output_file[0] = 0; }
    AnimationState(const AnimationState& o) { *this = o; }
    AnimationState& operator=(const AnimationState& o) {
        mode = o.mode;
        first_render = o.first_render;
        interp_method = o.interp_method; interp_speed = o.interp_speed; frame_rate = o.frame_rate;
        follow_up = o.follow_up; height_factor = o.height_factor; forward_factor = o.forward_factor;
        zoom_out_factor = o.zoom_out_factor; vert_offset_factor = o.vert_offset_factor;
        pitch_angle = o.pitch_angle; keyframe_samples = o.keyframe_samples; num_loops = o.num_loops;
        std::memcpy(output_file, o.output_file, sizeof(output_file));
        rec_active = o.rec_active; rec_cancel = o.rec_cancel;
        rec_frame = o.rec_frame; rec_ok = o.rec_ok; rec_fail = o.rec_fail;
        rec_w = o.rec_w; rec_h = o.rec_h;
        rec_path_was_visible = o.rec_path_was_visible;
        rec_cams_was_visible = o.rec_cams_was_visible;
        rec_stem = o.rec_stem; rec_ext = o.rec_ext;
        rec_frames = o.rec_frames;
        return *this;
    }
};

void renderDialogAnimation(ViewportCanvas* viewer, MainWindow* win,
                           AnimationState& s, bool& open);

#endif // CLAW3D_ANIMATION_DIALOG_H
