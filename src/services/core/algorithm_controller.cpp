// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/core/algorithm_controller.h"

#include <mutex>
#include <utility>

AlgorithmCompletionPolicy AlgorithmCompletionPolicy::immediate() {
    return {};
}

AlgorithmCompletionPolicy AlgorithmCompletionPolicy::preview_flush() {
    AlgorithmCompletionPolicy policy;
    policy.requires_preview_flush = true;
    return policy;
}

AlgorithmCompletionPolicy AlgorithmCompletionPolicy::final_preview_hold() {
    AlgorithmCompletionPolicy policy;
    policy.requires_final_preview_hold = true;
    return policy;
}

const char* algorithm_job_state_label(AlgorithmJobState state) {
    switch (state) {
    case AlgorithmJobState::Idle: return "Idle";
    case AlgorithmJobState::Starting: return "Starting";
    case AlgorithmJobState::Running: return "Running";
    case AlgorithmJobState::WorkerFinished: return "Worker finished";
    case AlgorithmJobState::FlushingPreview: return "Flushing preview";
    case AlgorithmJobState::HoldingFinalPreview: return "Holding final preview";
    case AlgorithmJobState::AwaitingUiCommit: return "Committing result";
    case AlgorithmJobState::Completed: return "Completed";
    case AlgorithmJobState::Cancelling: return "Cancelling";
    case AlgorithmJobState::Cancelled: return "Cancelled";
    case AlgorithmJobState::Failed: return "Failed";
    }
    return "Unknown";
}

AlgorithmController::~AlgorithmController() {
    join_worker();
}

bool AlgorithmController::is_running() const {
    return busy.load(std::memory_order_acquire);
}

bool AlgorithmController::is_running_id(AlgorithmId expected_id) const {
    if (!is_running())
        return false;
    std::lock_guard<std::mutex> lock(mutex);
    return id == expected_id;
}

AlgorithmId AlgorithmController::algorithm_id() const {
    return current_id();
}

AlgorithmId AlgorithmController::current_id() const {
    std::lock_guard<std::mutex> lock(mutex);
    return id;
}

std::string AlgorithmController::current_label() const {
    std::lock_guard<std::mutex> lock(mutex);
    return label;
}

ModelHandle AlgorithmController::current_source_handle() const {
    std::lock_guard<std::mutex> lock(mutex);
    return source_handle;
}

AlgorithmJobState AlgorithmController::state() const {
    return job_state.load(std::memory_order_acquire);
}

const char* AlgorithmController::state_label() const {
    return algorithm_job_state_label(state());
}

AlgorithmCompletionPolicy AlgorithmController::completion_policy() const {
    std::lock_guard<std::mutex> lock(mutex);
    return policy;
}

bool AlgorithmController::worker_finished() const {
    switch (state()) {
    case AlgorithmJobState::WorkerFinished:
    case AlgorithmJobState::FlushingPreview:
    case AlgorithmJobState::HoldingFinalPreview:
    case AlgorithmJobState::AwaitingUiCommit:
    case AlgorithmJobState::Completed:
    case AlgorithmJobState::Cancelled:
    case AlgorithmJobState::Failed:
        return true;
    case AlgorithmJobState::Idle:
    case AlgorithmJobState::Starting:
    case AlgorithmJobState::Running:
    case AlgorithmJobState::Cancelling:
        return false;
    }
    return false;
}

bool AlgorithmController::ready_for_ui_commit() const {
    return done.load(std::memory_order_acquire) ||
           state() == AlgorithmJobState::AwaitingUiCommit;
}

bool AlgorithmController::has_quality_context() const {
    std::lock_guard<std::mutex> lock(mutex);
    return !quality_context.empty() || !completed_quality_context.empty();
}

bool AlgorithmController::has_quality_context_for(
        AlgorithmId expected_id) const {
    std::lock_guard<std::mutex> lock(mutex);
    return (id == expected_id && !quality_context.empty()) ||
        (completed_quality_context_id == expected_id &&
         !completed_quality_context.empty());
}

std::string AlgorithmController::take_quality_context() {
    std::lock_guard<std::mutex> lock(mutex);
    std::string context;
    if (!quality_context.empty()) {
        context = std::move(quality_context);
        quality_context.clear();
    } else {
        context = std::move(completed_quality_context);
        completed_quality_context.clear();
        completed_quality_context_id = AlgorithmId::Unknown;
    }
    return context;
}

void AlgorithmController::begin(AlgorithmId next_id,
                                const std::string& next_label,
                                const ModelHandle& next_source_handle,
                                ResultDisposition next_disposition,
                                AlgorithmCompletionPolicy next_policy) {
    job_state.store(AlgorithmJobState::Starting, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex);
        source_handle = next_source_handle;
        id = next_id;
        policy = next_policy;
        disposition = next_disposition;
        label = next_label;
        error_message.clear();
        quality_context.clear();
        completed_quality_context_id = AlgorithmId::Unknown;
        completed_quality_context.clear();
        results.clear();
    }
    done.store(false, std::memory_order_release);
    busy.store(true, std::memory_order_release);
    job_state.store(AlgorithmJobState::Running, std::memory_order_release);
}

void AlgorithmController::request_cancel() {
    if (is_running())
        job_state.store(AlgorithmJobState::Cancelling,
                        std::memory_order_release);
}

void AlgorithmController::mark_worker_finished() {
    const AlgorithmJobState current = state();
    if (current == AlgorithmJobState::Cancelling) {
        job_state.store(AlgorithmJobState::Cancelled,
                        std::memory_order_release);
        return;
    }
    if (is_running() &&
        (current == AlgorithmJobState::Starting ||
         current == AlgorithmJobState::Running)) {
        job_state.store(AlgorithmJobState::WorkerFinished,
                        std::memory_order_release);
    }
}

void AlgorithmController::mark_preview_flushing() {
    if (is_running())
        job_state.store(AlgorithmJobState::FlushingPreview,
                        std::memory_order_release);
}

void AlgorithmController::mark_final_preview_holding() {
    if (is_running())
        job_state.store(AlgorithmJobState::HoldingFinalPreview,
                        std::memory_order_release);
}

void AlgorithmController::mark_ready_for_ui_commit() {
    AlgorithmJobState current = state();
    if (current == AlgorithmJobState::Cancelled ||
        current == AlgorithmJobState::Failed) {
        done.store(true, std::memory_order_release);
        return;
    }
    mark_worker_finished();
    current = state();
    if (current == AlgorithmJobState::Cancelled ||
        current == AlgorithmJobState::Failed) {
        done.store(true, std::memory_order_release);
        return;
    }
    job_state.store(AlgorithmJobState::AwaitingUiCommit,
                    std::memory_order_release);
    done.store(true, std::memory_order_release);
}

void AlgorithmController::mark_done() {
    mark_ready_for_ui_commit();
}

void AlgorithmController::mark_cancelled() {
    job_state.store(AlgorithmJobState::Cancelled, std::memory_order_release);
    done.store(true, std::memory_order_release);
}

void AlgorithmController::mark_failed(std::string error) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        error_message = std::move(error);
    }
    job_state.store(AlgorithmJobState::Failed, std::memory_order_release);
    done.store(true, std::memory_order_release);
}

void AlgorithmController::set_quality_context(std::string context) {
    std::lock_guard<std::mutex> lock(mutex);
    quality_context = std::move(context);
}

void AlgorithmController::push_owned_result(easy3d::Model* result) {
    if (!result)
        return;
    std::lock_guard<std::mutex> lock(mutex);
    results.emplace_back(result);
}

void AlgorithmController::push_result(std::unique_ptr<easy3d::Model> result) {
    if (!result)
        return;
    std::lock_guard<std::mutex> lock(mutex);
    results.push_back(std::move(result));
}

void AlgorithmController::start_worker(std::thread next_worker) {
    join_worker();
    worker = std::move(next_worker);
}

void AlgorithmController::join_worker() {
    if (worker.joinable())
        worker.join();
}

void AlgorithmController::clear_results_and_source() {
    std::lock_guard<std::mutex> lock(mutex);
    results.clear();
    source_handle = {};
    id = AlgorithmId::Unknown;
    policy = AlgorithmCompletionPolicy::immediate();
    disposition = ResultDisposition::AddAsChild;
    label.clear();
    error_message.clear();
    quality_context.clear();
    completed_quality_context_id = AlgorithmId::Unknown;
    completed_quality_context.clear();
    job_state.store(AlgorithmJobState::Idle, std::memory_order_release);
}

bool AlgorithmController::try_consume_completion(CompletionBatch& batch) {
    if (!done.exchange(false, std::memory_order_acq_rel))
        return false;

    const AlgorithmJobState terminal_state = state();
    std::lock_guard<std::mutex> lock(mutex);
    batch.id = id;
    batch.disposition = disposition;
    batch.label = label;
    batch.source_handle = source_handle;
    batch.results = std::move(results);

    completed_quality_context_id = quality_context.empty()
        ? AlgorithmId::Unknown
        : id;
    completed_quality_context = std::move(quality_context);
    results.clear();
    source_handle = {};
    id = AlgorithmId::Unknown;
    policy = AlgorithmCompletionPolicy::immediate();
    disposition = ResultDisposition::AddAsChild;
    label.clear();
    error_message.clear();
    quality_context.clear();
    busy.store(false, std::memory_order_release);
    if (terminal_state != AlgorithmJobState::Cancelled &&
        terminal_state != AlgorithmJobState::Failed) {
        job_state.store(AlgorithmJobState::Completed,
                        std::memory_order_release);
    }
    return true;
}
