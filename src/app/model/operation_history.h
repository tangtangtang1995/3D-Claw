// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_OPERATION_HISTORY_H
#define CLAW3D_OPERATION_HISTORY_H

/// Undo/redo operation records for scene and model editing commands.

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

enum class OperationStatus {
    Running,
    Success,
    Failed,
    Skipped
};

const char* operation_status_label(OperationStatus status);

// Minimal: a rolling record of algorithm runs so the user (and AI)
// can answer "what did I just do?". Captures label + source name +
// output names + wall-clock duration + success. No params replay yet --
// that needs a generic Algorithm Registry (not yet implemented).

struct HistoryEntry {
    int   id = 0;
    std::string label;                       // e.g. "Poisson Reconstruction"
    std::string source_name;                 // input model name (empty if N/A)
    std::vector<std::string> output_names;   // generated model names
    std::string status_detail;
    float elapsed_sec = 0.0f;
    bool  success = true;
    OperationStatus status = OperationStatus::Running;
    std::chrono::system_clock::time_point started;
    std::chrono::system_clock::time_point finished;
};

class OperationHistory {
public:
    static OperationHistory& instance();

    // Start a new operation; returns its id. Pass the id back to
    // finish_operation() when the run completes (or aborts).
    int  start_operation(const std::string& label, const std::string& source_name);
    void finish_operation(int id, const std::vector<std::string>& output_names,
                          bool success = true);
    void finish_operation(int id,
                          const std::vector<std::string>& output_names,
                          OperationStatus status,
                          const std::string& detail);

    // Mark id as failed/cancelled and finalize immediately. Equivalent to
    // finish_operation(id, {}, false), kept as a clear-intent shortcut.
    void cancel_operation(int id);

    const std::vector<HistoryEntry>& entries() const { return entries_; }
    void clear();

    // Most recent N entries as a compact block for AI context. Returns
    // empty when history has nothing.
    std::string format_for_ai(std::size_t max_entries = 5) const;

private:
    OperationHistory() = default;
    OperationHistory(const OperationHistory&) = delete;
    OperationHistory& operator=(const OperationHistory&) = delete;

    mutable std::mutex mu_;
    std::vector<HistoryEntry> entries_;
    int next_id_ = 1;
    static constexpr std::size_t kMaxEntries = 200;
};

#endif // CLAW3D_OPERATION_HISTORY_H
