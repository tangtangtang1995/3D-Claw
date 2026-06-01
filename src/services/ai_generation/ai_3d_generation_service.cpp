// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/ai_generation/ai_3d_generation_service.h"

#include "io/surface_mesh_io.h"
#include "services/core/algorithm_controller.h"
#include "services/ai_generation/archive_extract_utils.h"
#include "services/platform/platform_runtime.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/file_system.h>

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace claw3d::services {
namespace {

void notify_done(AlgorithmController& controller,
                 const std::function<void()>& wake_ui)
{
    controller.mark_done();
    if (wake_ui)
        wake_ui();
}

std::string generated_model_display_name(const std::string& job_id,
                                         const std::string& model_path)
{
    std::string name = "gen3d." + claw_3dgen::sanitize_path_component(job_id);
    const std::string ext = easy3d::file_system::extension(model_path, true);
    if (!ext.empty()) {
        name += ".";
        name += ext;
    }
    return name;
}

} // namespace

ThreeDGenerationJobHandle::~ThreeDGenerationJobHandle()
{
    join();
}

void ThreeDGenerationJobHandle::cancel_and_join(std::atomic<bool>& cancelled)
{
    cancelled.store(true, std::memory_order_release);
    join();
}

void ThreeDGenerationJobHandle::start(std::thread worker)
{
    join();
    worker_ = std::move(worker);
}

void ThreeDGenerationJobHandle::join()
{
    if (worker_.joinable())
        worker_.join();
}

bool start_three_d_generation_job(AlgorithmController& controller,
                                  ThreeDGenerationJobHandle& handle,
                                  const ThreeDGenerationJobStart& request)
{
    if (!request.cancelled)
        return false;

    handle.cancel_and_join(*request.cancelled);
    request.cancelled->store(false, std::memory_order_release);
    controller.begin(AlgorithmId::ThreeDGeneration,
                     "AI 3D Generation",
                     ModelHandle{},
                     ResultDisposition::AddAsChild);

    handle.start(std::thread([&controller, request]() {
        try {
            platform::lower_current_thread_priority();

            claw_3dgen::GenerationJobOptions options;
            options.request = request.request;
            options.cancelled = request.cancelled;
            options.on_status =
                [on_status = request.on_status,
                 wake_ui = request.wake_ui](
                    const claw_3dgen::GenerationJobStatus& status) {
                    if (on_status)
                        on_status(status);
                    if (wake_ui)
                        wake_ui();
                };

            auto result = claw_3dgen::run_hunyuan_3d_generation_job(options);
            if (result.cancelled) {
                if (request.on_status) {
                    request.on_status({claw_3dgen::GenerationJobPhase::Cancelled,
                                       "Cancelled",
                                       result.job_id,
                                       result.credits_consumed});
                }
                notify_done(controller, request.wake_ui);
                return;
            }

            if (!result.success) {
                if (request.on_status) {
                    request.on_status({claw_3dgen::GenerationJobPhase::Error,
                                       result.error_message,
                                       result.job_id,
                                       result.credits_consumed});
                }
                notify_done(controller, request.wake_ui);
                return;
            }

            if (request.cancelled->load(std::memory_order_acquire)) {
                if (request.on_status) {
                    request.on_status({claw_3dgen::GenerationJobPhase::Cancelled,
                                       "Cancelled",
                                       result.job_id,
                                       result.credits_consumed});
                }
                notify_done(controller, request.wake_ui);
                return;
            }

            std::unique_ptr<easy3d::SurfaceMesh> mesh(
                claw3d::io::load_surface_mesh(result.model_path));
            if (!mesh) {
                if (request.on_status) {
                    request.on_status({claw_3dgen::GenerationJobPhase::Error,
                                       "Cannot load generated model",
                                       result.job_id,
                                       result.credits_consumed});
                }
                notify_done(controller, request.wake_ui);
                return;
            }

            mesh->set_name(generated_model_display_name(result.job_id,
                                                        result.model_path));
            controller.push_owned_result(mesh.release());
            if (request.on_status) {
                claw_3dgen::GenerationJobStatus status;
                status.phase = claw_3dgen::GenerationJobPhase::Done;
                status.message = "Done: " +
                    std::to_string(result.credits_consumed) + " credits";
                status.job_id = result.job_id;
                status.model_path = result.model_path;
                status.credits_consumed = result.credits_consumed;
                request.on_status(status);
            }
            notify_done(controller, request.wake_ui);
        } catch (const std::exception& e) {
            if (request.on_status) {
                request.on_status({claw_3dgen::GenerationJobPhase::Error,
                                   std::string("3D generation failed: ") +
                                       e.what(),
                                   std::string(),
                                   0});
            }
            notify_done(controller, request.wake_ui);
        } catch (...) {
            if (request.on_status) {
                request.on_status({claw_3dgen::GenerationJobPhase::Error,
                                   "3D generation failed: unknown exception",
                                   std::string(),
                                   0});
            }
            notify_done(controller, request.wake_ui);
        }
    }));
    return true;
}

} // namespace claw3d::services
