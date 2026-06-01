// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/io/file_load_controller.h"

#include <easy3d/core/model.h>

#include <utility>

FileLoadController::~FileLoadController() {
    join_worker();
}

bool FileLoadController::is_busy() const {
    return loading_.load() || uploading_.load();
}

bool FileLoadController::is_uploading() const {
    return uploading_.load();
}

bool FileLoadController::can_start() const {
    return !is_busy();
}

void FileLoadController::begin_loading() {
    loading_ = true;
    loading_done_ = false;
}

void FileLoadController::finish_loading(
        std::vector<std::unique_ptr<easy3d::Model>> models) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loaded_models_ = std::move(models);
    }
    loading_done_ = true;
}

void FileLoadController::start_worker(std::thread worker) {
    join_worker();
    worker_ = std::move(worker);
}

void FileLoadController::join_worker() {
    if (worker_.joinable())
        worker_.join();
}

bool FileLoadController::process_pending_upload(
        std::vector<std::unique_ptr<easy3d::Model>>& models_to_upload) {
    if (loading_done_.exchange(false)) {
        loading_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_upload_models_ = std::move(loaded_models_);
            loaded_models_.clear();
        }
        uploading_ = true;
        upload_frame_count_ = 0;
    }

    bool do_upload_this_frame = false;
    if (uploading_) {
        if (upload_frame_count_ == 1)
            do_upload_this_frame = true;
        ++upload_frame_count_;
    }

    if (!do_upload_this_frame)
        return false;

    models_to_upload = std::move(pending_upload_models_);
    pending_upload_models_.clear();
    return true;
}

void FileLoadController::finish_upload() {
    uploading_ = false;
}
