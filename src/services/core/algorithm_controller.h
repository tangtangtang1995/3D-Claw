// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALGORITHM_CONTROLLER_H
#define CLAW3D_ALGORITHM_CONTROLLER_H

/// App/service boundary object that owns asynchronous algorithm lifecycles.

#include "common/model_handle.h"
#include "services/core/algorithm_id.h"

#include <easy3d/core/model.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Coordinates the lifecycle of one active backend algorithm job.
/// Owns worker completion state and transfers produced models back to the UI.
class AlgorithmController {
public:
    ~AlgorithmController();
    AlgorithmController() = default;
    AlgorithmController(const AlgorithmController&) = delete;
    AlgorithmController& operator=(const AlgorithmController&) = delete;
    AlgorithmController(AlgorithmController&&) = delete;
    AlgorithmController& operator=(AlgorithmController&&) = delete;

    /// Result payload moved from a finished worker to the UI thread.
    struct CompletionBatch {
        AlgorithmId id = AlgorithmId::Unknown;
        ResultDisposition disposition = ResultDisposition::AddAsChild;
        std::string label;
        ModelHandle source_handle;
        std::vector<std::unique_ptr<easy3d::Model>> results;
    };

    bool is_running() const;
    bool is_running_id(AlgorithmId expected_id) const;
    AlgorithmId current_id() const;
    std::string current_label() const;
    ModelHandle current_source_handle() const;
    bool has_quality_context() const;
    bool has_quality_context_for(AlgorithmId expected_id) const;
    std::string take_quality_context();

    /// Starts a new algorithm lifecycle before the worker thread is launched.
    void begin(AlgorithmId id,
               const std::string& label,
               const ModelHandle& source_handle,
               ResultDisposition disposition);
    void mark_done();
    void set_quality_context(std::string context);
    void push_owned_result(easy3d::Model* result);
    void push_result(std::unique_ptr<easy3d::Model> result);
    /// Takes ownership of a worker thread for the active algorithm run.
    void start_worker(std::thread worker);
    void join_worker();
    void clear_results_and_source();
    /// Moves the completed result batch to the caller when the worker is done.
    bool try_consume_completion(CompletionBatch& batch);

private:
    std::atomic<bool> busy{false};
    std::atomic<bool> done{false};
    AlgorithmId id = AlgorithmId::Unknown;
    ResultDisposition disposition = ResultDisposition::AddAsChild;
    std::string label;
    std::string quality_context;
    AlgorithmId completed_quality_context_id = AlgorithmId::Unknown;
    std::string completed_quality_context;
    ModelHandle source_handle;
    std::thread worker;
    mutable std::mutex mutex;
    std::vector<std::unique_ptr<easy3d::Model>> results;
};

#endif // CLAW3D_ALGORITHM_CONTROLLER_H
