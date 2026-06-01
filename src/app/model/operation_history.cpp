// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "model/operation_history.h"

#include <ctime>
#include <iomanip>
#include <sstream>


const char* operation_status_label(OperationStatus status) {
    switch (status) {
    case OperationStatus::Running: return "RUNNING";
    case OperationStatus::Success: return "OK";
    case OperationStatus::Failed: return "FAIL";
    case OperationStatus::Skipped: return "SKIP";
    }
    return "UNKNOWN";
}


OperationHistory& OperationHistory::instance() {
    static OperationHistory s;
    return s;
}


int OperationHistory::start_operation(const std::string& label,
                                      const std::string& source_name)
{
    std::lock_guard<std::mutex> lk(mu_);
    HistoryEntry e;
    e.id = next_id_++;
    e.label = label.empty() ? "(unnamed)" : label;
    e.source_name = source_name;
    e.started = std::chrono::system_clock::now();
    e.finished = e.started;
    e.success = false;          // flipped to true on success
    e.status = OperationStatus::Running;
    entries_.push_back(std::move(e));

    // Cap memory: drop the oldest entries when we exceed the cap. Keep id
    // monotonic so finish lookups still work for entries still in the
    // window.
    if (entries_.size() > kMaxEntries) {
        const std::size_t drop = entries_.size() - kMaxEntries;
        entries_.erase(entries_.begin(), entries_.begin() + drop);
    }
    return entries_.back().id;
}


void OperationHistory::finish_operation(int id,
                                        const std::vector<std::string>& outputs,
                                        bool success)
{
    finish_operation(id, outputs,
                     success ? OperationStatus::Success : OperationStatus::Failed,
                     {});
}


void OperationHistory::finish_operation(int id,
                                        const std::vector<std::string>& outputs,
                                        OperationStatus status,
                                        const std::string& detail)
{
    std::lock_guard<std::mutex> lk(mu_);
    // Search from the end -- finish typically matches the most recent start.
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->id != id) continue;
        it->output_names = outputs;
        it->status = status;
        it->success = status == OperationStatus::Success;
        it->status_detail = detail;
        it->finished = std::chrono::system_clock::now();
        it->elapsed_sec = std::chrono::duration<float>(it->finished - it->started).count();
        return;
    }
}


void OperationHistory::cancel_operation(int id) {
    finish_operation(id, {}, false);
}


void OperationHistory::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();
    next_id_ = 1;
}


namespace {
std::string short_time(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(2) << tm.tm_hour << ':'
       << std::setw(2) << tm.tm_min  << ':'
       << std::setw(2) << tm.tm_sec;
    return os.str();
}
}


std::string OperationHistory::format_for_ai(std::size_t max_entries) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (entries_.empty()) return {};
    std::ostringstream os;
    const std::size_t total = entries_.size();
    const std::size_t start = total > max_entries ? total - max_entries : 0;
    os << "Recent operations (oldest -> newest, last " << (total - start) << "):";
    for (std::size_t i = start; i < total; ++i) {
        const auto& e = entries_[i];
        os << "\n  " << (i - start + 1) << ". "
           << short_time(e.started) << " "
           << e.label;
        if (!e.source_name.empty()) os << " on \"" << e.source_name << "\"";
        if (!e.output_names.empty()) {
            os << " -> ";
            for (std::size_t k = 0; k < e.output_names.size(); ++k) {
                if (k) os << ", ";
                os << '\"' << e.output_names[k] << '\"';
                if (k >= 2 && e.output_names.size() > 3) {
                    os << " (+" << (e.output_names.size() - k - 1) << " more)";
                    break;
                }
            }
        }
        os << "  [" << operation_status_label(e.status)
           << ", " << e.elapsed_sec << "s]";
        if (!e.status_detail.empty())
            os << " " << e.status_detail;
    }
    return os.str();
}
