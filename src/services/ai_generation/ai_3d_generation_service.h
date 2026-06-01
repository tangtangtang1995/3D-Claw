// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_AI_3D_GENERATION_SERVICE_H
#define CLAW3D_SERVICES_AI_3D_GENERATION_SERVICE_H

/// Service facade used by the UI to launch and observe 3D generation jobs.

#include "services/ai_generation/ai_3d_generation_job.h"

#include <atomic>
#include <functional>
#include <thread>

class AlgorithmController;

namespace claw3d::services {

/// UI-facing handle for an asynchronous backend job.
/// Keeps concrete runner types out of the app layer.
class ThreeDGenerationJobHandle {
public:
    ~ThreeDGenerationJobHandle();
    ThreeDGenerationJobHandle() = default;
    ThreeDGenerationJobHandle(const ThreeDGenerationJobHandle&) = delete;
    ThreeDGenerationJobHandle& operator=(const ThreeDGenerationJobHandle&) = delete;

    void cancel_and_join(std::atomic<bool>& cancelled);
    void start(std::thread worker);

private:
    void join();

    std::thread worker_;
};

/// Start request captured before the backend worker is launched.
struct ThreeDGenerationJobStart {
    claw_3dgen::Hunyuan3DRequest request;
    std::atomic<bool>* cancelled = nullptr;
    std::function<void(const claw_3dgen::GenerationJobStatus&)> on_status;
    std::function<void()> wake_ui;
};

bool start_three_d_generation_job(AlgorithmController& controller,
                                  ThreeDGenerationJobHandle& handle,
                                  const ThreeDGenerationJobStart& request);

} // namespace claw3d::services

#endif // CLAW3D_SERVICES_AI_3D_GENERATION_SERVICE_H
