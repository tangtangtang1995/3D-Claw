// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_AI_3D_GENERATION_JOB_H
#define CLAW3D_SERVICES_AI_3D_GENERATION_JOB_H

/// Worker-level orchestration contract for Hunyuan 3D generation jobs.

#include "services/ai_generation/ai_3d_generation_client.h"

#include <atomic>
#include <functional>
#include <string>

namespace claw_3dgen {

/// User-visible phase of the long-running generation workflow.
enum class GenerationJobPhase {
    Submitting,
    Polling,
    Downloading,
    Done,
    Error,
    Cancelled
};

/// Status callback payload emitted by the generation worker.
struct GenerationJobStatus {
    GenerationJobPhase phase = GenerationJobPhase::Submitting;
    std::string message;
    std::string job_id;
    int credits_consumed = 0;
    std::string model_path;
};

/// Worker input and callbacks for a Hunyuan generation run.
struct GenerationJobOptions {
    Hunyuan3DRequest request;
    std::atomic<bool>* cancelled = nullptr;
    std::function<void(const GenerationJobStatus&)> on_status;
    int poll_interval_seconds = 3;
};

/// Final worker outcome after download and archive extraction.
struct GenerationJobResult {
    bool success = false;
    bool cancelled = false;
    std::string job_id;
    std::string model_path;
    std::string error_message;
    int credits_consumed = 0;
};

/// Runs submit, poll, download, and archive extraction on a worker thread.
GenerationJobResult run_hunyuan_3d_generation_job(
    const GenerationJobOptions& options);

} // namespace claw_3dgen

#endif // CLAW3D_SERVICES_AI_3D_GENERATION_JOB_H
