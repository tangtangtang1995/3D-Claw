// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/ai_generation/ai_3d_generation_job.h"

#include "services/ai_generation/archive_extract_utils.h"

#include <chrono>
#include <thread>

namespace claw_3dgen {
namespace {

bool is_cancelled(const GenerationJobOptions& options) {
    return options.cancelled && options.cancelled->load(std::memory_order_acquire);
}

void notify(const GenerationJobOptions& options,
            GenerationJobPhase phase,
            const std::string& message,
            const std::string& job_id = std::string(),
            int credits = 0,
            const std::string& model_path = std::string()) {
    if (!options.on_status)
        return;
    GenerationJobStatus status;
    status.phase = phase;
    status.message = message;
    status.job_id = job_id;
    status.model_path = model_path;
    status.credits_consumed = credits;
    options.on_status(status);
}

bool sleep_with_cancel(const GenerationJobOptions& options, int seconds) {
    const int steps = seconds * 10;
    for (int i = 0; i < steps; ++i) {
        if (is_cancelled(options))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

GenerationJobResult fail_result(const std::string& job_id,
                                const std::string& error,
                                int credits) {
    GenerationJobResult result;
    result.success = false;
    result.job_id = job_id;
    result.error_message = error;
    result.credits_consumed = credits;
    return result;
}

GenerationJobResult cancel_result(const std::string& job_id, int credits) {
    GenerationJobResult result;
    result.success = false;
    result.cancelled = true;
    result.job_id = job_id;
    result.error_message = "Cancelled";
    result.credits_consumed = credits;
    return result;
}

} // namespace

GenerationJobResult run_hunyuan_3d_generation_job(
        const GenerationJobOptions& options) {
    Hunyuan3DClient client;
    std::string job_id;
    std::string error;
    int credits = 0;

    notify(options, GenerationJobPhase::Submitting, "Submitting job...");
    if (is_cancelled(options)) {
        notify(options, GenerationJobPhase::Cancelled, "Cancelled");
        return cancel_result(job_id, credits);
    }

    if (!client.submit_job(options.request, job_id, error,
                           options.cancelled)) {
        if (is_cancelled(options)) {
            notify(options, GenerationJobPhase::Cancelled, "Cancelled");
            return cancel_result(job_id, credits);
        }
        notify(options, GenerationJobPhase::Error, "Submit: " + error);
        return fail_result(job_id, "Submit: " + error, credits);
    }

    notify(options, GenerationJobPhase::Polling, "Polling...", job_id);
    int polls = 0;
    const int poll_interval = options.poll_interval_seconds > 0 ?
        options.poll_interval_seconds : 3;

    while (!is_cancelled(options)) {
        if (!sleep_with_cancel(options, poll_interval))
            break;
        ++polls;
        notify(options, GenerationJobPhase::Polling,
               "Polling... (" + std::to_string(polls * poll_interval) + "s)",
               job_id, credits);

        Hunyuan3DQueryResult query;
        if (!client.query_job(options.request, job_id, query, error,
                              options.cancelled)) {
            if (is_cancelled(options)) {
                notify(options, GenerationJobPhase::Cancelled, "Cancelled",
                       job_id, credits);
                return cancel_result(job_id, credits);
            }
            notify(options, GenerationJobPhase::Error, "Query: " + error,
                   job_id, credits);
            return fail_result(job_id, "Query: " + error, credits);
        }
        credits = query.credits_consumed;

        if (query.state == HunyuanQueryState::Error) {
            notify(options, GenerationJobPhase::Error, query.error_message,
                   job_id, credits);
            return fail_result(job_id, query.error_message, credits);
        }

        if (query.state != HunyuanQueryState::Done)
            continue;

        if (query.result_urls.empty()) {
            notify(options, GenerationJobPhase::Error, "Missing result URL",
                   job_id, credits);
            return fail_result(job_id, "Missing result URL", credits);
        }

        notify(options, GenerationJobPhase::Downloading, "Downloading...",
               job_id, credits);

        std::string archive_body;
        if (!client.download_url(query.result_urls.front(), archive_body,
                                 error, options.cancelled)) {
            if (is_cancelled(options)) {
                notify(options, GenerationJobPhase::Cancelled, "Cancelled",
                       job_id, credits);
                return cancel_result(job_id, credits);
            }
            notify(options, GenerationJobPhase::Error, error, job_id, credits);
            return fail_result(job_id, error, credits);
        }

        if (is_cancelled(options)) {
            notify(options, GenerationJobPhase::Cancelled, "Cancelled",
                   job_id, credits);
            return cancel_result(job_id, credits);
        }

        std::string model_path;
        if (!save_generation_archive_and_find_model(job_id, archive_body,
                                                    model_path, error)) {
            notify(options, GenerationJobPhase::Error, error, job_id, credits);
            return fail_result(job_id, error, credits);
        }

        GenerationJobResult result;
        result.success = true;
        result.job_id = job_id;
        result.model_path = model_path;
        result.credits_consumed = credits;
        notify(options, GenerationJobPhase::Done,
               "Done: " + std::to_string(credits) + " credits",
               job_id, credits, model_path);
        return result;
    }

    notify(options, GenerationJobPhase::Cancelled, "Cancelled", job_id,
           credits);
    return cancel_result(job_id, credits);
}

} // namespace claw_3dgen
