// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_CGAL_SIMPLIFICATION_DIALOG_H
#define CLAW3D_CGAL_SIMPLIFICATION_DIALOG_H

/// State and render entry point for CGAL mesh simplification controls.

#include <atomic>
#include <deque>
#include <string>
#include <vector>

#include "common/preview_policy.h"
#include "services/jobs/cgal/cgal_simplification_job.h"

class ViewportCanvas;

struct CGALSimplificationState {
    CGALSimplificationState() = default;
    // std::atomic members forbid the implicit copy ctor / copy assign. We
    // never deep-copy this state in practice (it lives in MainWindow as
    // a single instance), but the compiler still wants the operators defined
    // when std::atomic<bool> is a member, so spell them out explicitly.
    CGALSimplificationState(const CGALSimplificationState& other) { *this = other; }
    CGALSimplificationState& operator=(const CGALSimplificationState& other) {
        strategy        = other.strategy;
        stop_mode       = other.stop_mode;
        target_ratio    = other.target_ratio;
        target_count    = other.target_count;
        live_preview    = other.live_preview;
        use_bounded_normal_change = other.use_bounded_normal_change;
        use_polyhedral_envelope = other.use_polyhedral_envelope;
        envelope_relative_percent = other.envelope_relative_percent;
        snapshot_min_ms = other.snapshot_min_ms;
        preview_speed   = other.preview_speed;
        running         = other.running;
        runner          = other.runner;
        last_stats      = other.last_stats;
        last_stats_valid = other.last_stats_valid;
        last_snapshot_gen_seen = other.last_snapshot_gen_seen;
        pending_snapshots = other.pending_snapshots;
        last_snapshot_display_time = other.last_snapshot_display_time;
        last_trail_display_time = other.last_trail_display_time;
        last_run_strategy = other.last_run_strategy;
        last_run_stop_mode = other.last_run_stop_mode;
        last_run_target_ratio = other.last_run_target_ratio;
        last_run_target_count = other.last_run_target_count;
        last_run_live_preview = other.last_run_live_preview;
        last_run_preview_speed = other.last_run_preview_speed;
        last_run_bounded_normal_change = other.last_run_bounded_normal_change;
        last_run_polyhedral_envelope = other.last_run_polyhedral_envelope;
        last_run_envelope_epsilon = other.last_run_envelope_epsilon;
        last_run_source_name = other.last_run_source_name;
        last_input_metadata_prompt = other.last_input_metadata_prompt;
        final_result_ready.store(
            other.final_result_ready.load(std::memory_order_acquire),
            std::memory_order_release);
        return *this;
    }

    struct SnapshotFrame {
        std::vector<SIMPL_Point3d> verts;
        std::vector<SIMPL_Triangle> tris;
    };

    // ---- user parameters ----
    int    strategy     = SIMPL_STRAT_LindstromTurk;
    int    stop_mode    = SIMPL_STOP_EdgeRatio;
    float  target_ratio = 0.5f;
    int    target_count = 0;
    bool   live_preview = false;   // Live-preview toggle
    bool   use_bounded_normal_change = false;
    bool   use_polyhedral_envelope = false;
    float  envelope_relative_percent = 0.5f; // percent of source bbox diagonal
    // Live preview tuning (sliders in the dialog).
    int    snapshot_min_ms = claw3d::preview_policy::kDefaultSnapshotMinMs;
    int    preview_speed = 0;      // 0 = normal, 1 = slow visual playback

    // ---- runtime ----
    claw3d::services::CgalSimplificationJobHandle runner;
    bool   running          = false;
    SIMPL_DebugStats last_stats{};
    bool   last_stats_valid = false;
    std::atomic<bool> final_result_ready{true};
    // Snapshot generation seen by the UI. The runner publishes generations
    // 1..N as the simplification runs; whenever this falls behind, the UI
    // pulls the freshest mesh into the snapshot overlay.
    int    last_snapshot_gen_seen = -1;
    std::deque<SnapshotFrame> pending_snapshots;
    double last_snapshot_display_time = 0.0;
    double last_trail_display_time = 0.0;

    int    last_run_strategy = SIMPL_STRAT_LindstromTurk;
    int    last_run_stop_mode = SIMPL_STOP_EdgeRatio;
    float  last_run_target_ratio = 0.5f;
    int    last_run_target_count = 0;
    bool   last_run_live_preview = false;
    int    last_run_preview_speed = 0;
    bool   last_run_bounded_normal_change = false;
    bool   last_run_polyhedral_envelope = false;
    double last_run_envelope_epsilon = 0.0;
    std::string last_run_source_name;
    std::string last_input_metadata_prompt;
};

void renderDialogCGALSimplification(ViewportCanvas* viewer,
                                    CGALSimplificationState& s,
                                    bool& open);

#endif  // CLAW3D_CGAL_SIMPLIFICATION_DIALOG_H
