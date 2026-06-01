// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "services/ai_generation/archive_extract_utils.h"

#include <miniz.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using ZipEntry = std::pair<std::string, std::string>;

std::string make_zip_body(const std::vector<ZipEntry>& entries) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
        return {};

    for (const auto& entry : entries) {
        if (!mz_zip_writer_add_mem(&zip, entry.first.c_str(),
                                   entry.second.data(), entry.second.size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            return {};
        }
    }

    void* data = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &data, &size)) {
        mz_zip_writer_end(&zip);
        return {};
    }
    std::string body(static_cast<const char*>(data), size);
    mz_free(data);
    mz_zip_writer_end(&zip);
    return body;
}

void cleanup_output_dir(const std::string& model_path) {
    std::filesystem::path dir = std::filesystem::u8path(model_path).parent_path();
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (std::filesystem::exists(dir / "model.zip")) {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            return;
        }
        dir = dir.parent_path();
    }
}

bool require(bool condition, const std::string& message) {
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    const std::string valid_zip = make_zip_body({
        {"nested/generated.stl", "solid placeholder\nendsolid placeholder\n"},
        {"nested/generated.obj", "o generated\n"},
    });
    if (!require(!valid_zip.empty(), "failed to create valid test zip"))
        return 1;

    std::string model_path;
    std::string error;
    if (!require(claw_3dgen::save_generation_archive_and_find_model(
                     "archive_test", valid_zip, model_path, error),
                 "valid archive was rejected: " + error))
        return 1;
    if (!require(model_path.find("generated.obj") != std::string::npos,
                 "preferred OBJ model was not selected: " + model_path))
        return 1;
    cleanup_output_dir(model_path);

    const std::string traversal_zip = make_zip_body({{"../evil.obj", "o evil\n"}});
    if (!require(!traversal_zip.empty(), "failed to create traversal test zip"))
        return 1;

    model_path.clear();
    error.clear();
    if (!require(!claw_3dgen::save_generation_archive_and_find_model(
                     "archive_slip_test", traversal_zip, model_path, error),
                 "path traversal archive was accepted"))
        return 1;
    if (!require(error.find("parent-directory") != std::string::npos,
                 "unexpected traversal error: " + error))
        return 1;

    return 0;
}