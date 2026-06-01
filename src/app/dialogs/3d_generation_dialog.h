// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_3D_GENERATION_DIALOG_H
#define CLAW3D_3D_GENERATION_DIALOG_H

/// State and render entry point for the AI 3D generation panel.

#include "services/ai_generation/ai_3d_generation_service.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

class ViewportCanvas;

enum Gen3DInputMode { GEN3D_INPUT_Image = 0, GEN3D_INPUT_Text = 1 };

enum Gen3DState { GEN3D_Idle, GEN3D_Submitting, GEN3D_Polling, GEN3D_Downloading, GEN3D_Done, GEN3D_Error };

struct Gen3DStatus {
    int state = GEN3D_Idle;
    char message[512] = "";
    float progress = 0.0f;
    int credits_consumed = 0;
};

struct Gen3DDialogState {
    Gen3DDialogState() {
        image_path[0] = 0;
        text_prompt[0] = 0;
        secret_id[0] = 0;
        secret_key[0] = 0;
        last_job_id[0] = 0;
        last_model_path[0] = 0;
        submitted_image_path[0] = 0;
        submitted_text_prompt[0] = 0;
    }
    ~Gen3DDialogState() { stop_worker(); }
    Gen3DDialogState(const Gen3DDialogState& other) { *this = other; }
    Gen3DDialogState& operator=(const Gen3DDialogState& other) {
        if (this == &other)
            return *this;
        stop_worker();
        std::lock_guard<std::mutex> lock(other.status_mutex);
        input_mode    = other.input_mode;
        std::memcpy(image_path, other.image_path, sizeof(image_path));
        std::memcpy(text_prompt, other.text_prompt, sizeof(text_prompt));
        std::memcpy(secret_id, other.secret_id, sizeof(secret_id));
        std::memcpy(secret_key, other.secret_key, sizeof(secret_key));
        generate_type = other.generate_type;
        enable_pbr    = other.enable_pbr;
        face_count    = other.face_count;
        std::memcpy(status.message, other.status.message,
                    sizeof(status.message));
        status.state         = other.status.state;
        status.progress      = other.status.progress;
        status.credits_consumed = other.status.credits_consumed;
        std::memcpy(last_job_id, other.last_job_id, sizeof(last_job_id));
        std::memcpy(last_model_path, other.last_model_path,
                    sizeof(last_model_path));
        close_requested = other.close_requested;
        cancelled.store(other.cancelled.load(std::memory_order_acquire),
                        std::memory_order_release);
        auto_evaluate    = other.auto_evaluate;
        ai_eval_done.store(other.ai_eval_done.load(std::memory_order_acquire),
                           std::memory_order_release);
        submitted_input_mode    = other.submitted_input_mode;
        submitted_generate_type = other.submitted_generate_type;
        submitted_enable_pbr    = other.submitted_enable_pbr;
        submitted_face_count    = other.submitted_face_count;
        std::memcpy(submitted_image_path, other.submitted_image_path,
                    sizeof(submitted_image_path));
        std::memcpy(submitted_text_prompt, other.submitted_text_prompt,
                    sizeof(submitted_text_prompt));
        return *this;
    }

    void stop_worker() {
        worker.cancel_and_join(cancelled);
    }

    int  input_mode    = GEN3D_INPUT_Image;
    char image_path[512];
    char text_prompt[1024];
    char secret_id[128];
    char secret_key[128];
    int  generate_type = 0;
    bool enable_pbr    = false;
    int  face_count    = claw_3dgen::kDefaultHunyuanFaceCount;

    mutable std::mutex status_mutex;
    Gen3DStatus status;
    char last_job_id[64];
    char last_model_path[1024];

    bool close_requested = false;
    std::atomic<bool> cancelled{false};
    claw3d::services::ThreeDGenerationJobHandle worker;

    // --- Auto-evaluate generated model with AI ---
    // When the generation finishes, the dialog can fire an AI Chat message
    // that asks the model to judge the result given (a) the original input,
    // (b) the generation params, and (c) the mesh metadata. ai_eval_done is
    // a one-shot guard so we don't dispatch the same evaluation twice across
    // frames. Cleared back to false at the start of each new submission.
    bool             auto_evaluate = true;
    std::atomic<bool> ai_eval_done{false};

    // Snapshot of the user's input AT submission time, so the eval prompt
    // can quote what was actually sent even if the user then edits the
    // form to prepare a next job.
    int  submitted_input_mode    = GEN3D_INPUT_Image;
    int  submitted_generate_type = 0;
    bool submitted_enable_pbr    = false;
    int  submitted_face_count    = claw_3dgen::kDefaultHunyuanFaceCount;
    char submitted_image_path[512];
    char submitted_text_prompt[1024];
};

void renderDialog3DGeneration(ViewportCanvas* viewer, Gen3DDialogState& s, bool& open);

#endif // CLAW3D_3D_GENERATION_DIALOG_H
