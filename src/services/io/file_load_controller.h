// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_FILE_LOAD_CONTROLLER_H
#define CLAW3D_SERVICES_FILE_LOAD_CONTROLLER_H

/// Async file-loading state machine that hands finished models to the UI.

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace easy3d { class Model; }

class FileLoadController {
public:
    ~FileLoadController();

    bool is_busy() const;
    bool is_uploading() const;
    bool can_start() const;

    void begin_loading();
    void finish_loading(std::vector<std::unique_ptr<easy3d::Model>> models);
    void start_worker(std::thread worker);
    void join_worker();

    bool process_pending_upload(
        std::vector<std::unique_ptr<easy3d::Model>>& models_to_upload);
    void finish_upload();

private:
    std::atomic<bool> loading_{false};
    std::atomic<bool> loading_done_{false};
    std::atomic<bool> uploading_{false};
    int upload_frame_count_ = 0;
    std::mutex mutex_;
    std::vector<std::unique_ptr<easy3d::Model>> loaded_models_;
    std::vector<std::unique_ptr<easy3d::Model>> pending_upload_models_;
    std::thread worker_;
};

#endif // CLAW3D_SERVICES_FILE_LOAD_CONTROLLER_H
