// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/core/algorithm_controller.h"

#include <mutex>
#include <utility>

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
                                ResultDisposition next_disposition) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        source_handle = next_source_handle;
        id = next_id;
        disposition = next_disposition;
        label = next_label;
        quality_context.clear();
        completed_quality_context_id = AlgorithmId::Unknown;
        completed_quality_context.clear();
        results.clear();
    }
    done.store(false, std::memory_order_release);
    busy.store(true, std::memory_order_release);
}

void AlgorithmController::mark_done() {
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
    disposition = ResultDisposition::AddAsChild;
    label.clear();
    quality_context.clear();
    completed_quality_context_id = AlgorithmId::Unknown;
    completed_quality_context.clear();
}

bool AlgorithmController::try_consume_completion(CompletionBatch& batch) {
    if (!done.exchange(false, std::memory_order_acq_rel))
        return false;

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
    disposition = ResultDisposition::AddAsChild;
    label.clear();
    quality_context.clear();
    busy.store(false, std::memory_order_release);
    return true;
}
