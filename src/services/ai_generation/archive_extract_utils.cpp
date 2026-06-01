// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/ai_generation/archive_extract_utils.h"
#include "services/platform/platform_paths.h"

#include <easy3d/util/file_system.h>

#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace claw_3dgen {
namespace {

namespace fs = std::filesystem;

constexpr int kMaxOutputDirectoryAttempts = 100;
constexpr int kUnsupportedModelPriority = 100;
constexpr std::size_t kMaxSanitizedJobIdLength = 48;
constexpr mz_uint kMaxZipEntryCount = 1024;
constexpr mz_uint64 kMaxZipEntryBytes = 512ull * 1024ull * 1024ull;
constexpr mz_uint64 kMaxZipTotalBytes = 1024ull * 1024ull * 1024ull;

struct FileWriteContext {
    std::ofstream* stream = nullptr;
};

std::string path_to_utf8(const fs::path& path) {
#if defined(__cpp_char8_t)
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
#else
    return path.u8string();
#endif
}

std::string generation_root_dir() {
    return claw3d::platform::generated_models_directory();
}

std::string make_generation_output_dir(const std::string& job_id) {
    const std::string base_dir = generation_root_dir();
    if (!easy3d::file_system::is_directory(base_dir) &&
        !easy3d::file_system::create_directory(base_dir))
        return std::string();

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const std::string base_name =
        sanitize_path_component(job_id) + "_" + std::to_string(stamp);
    for (int i = 0; i < kMaxOutputDirectoryAttempts; ++i) {
        std::string dir = base_dir + "/" + base_name;
        if (i > 0)
            dir += "_" + std::to_string(i);
        if (!easy3d::file_system::is_directory(dir) &&
            easy3d::file_system::create_directory(dir))
            return dir;
    }
    return std::string();
}

bool write_binary_file(const std::string& path,
                       const std::string& body,
                       std::string& error_out) {
    std::ofstream ofs(fs::u8path(path), std::ios::binary);
    if (!ofs) {
        error_out = "Cannot write archive";
        return false;
    }
    ofs.write(body.c_str(), static_cast<std::streamsize>(body.size()));
    if (!ofs) {
        error_out = "Archive write failed";
        return false;
    }
    return true;
}

bool is_supported_model_extension(const std::string& ext) {
    return ext == "obj" || ext == "glb" || ext == "gltf" ||
           ext == "ply" || ext == "off" || ext == "stl";
}

int model_extension_priority(const std::string& ext) {
    if (ext == "obj") return 0;
    if (ext == "glb") return 1;
    if (ext == "gltf") return 2;
    if (ext == "ply") return 3;
    if (ext == "off") return 4;
    if (ext == "stl") return 5;
    return kUnsupportedModelPriority;
}

std::string zip_error(mz_zip_archive& zip) {
    const char* message = mz_zip_get_error_string(mz_zip_get_last_error(&zip));
    return message ? std::string(message) : std::string("unknown ZIP error");
}

bool has_windows_drive_prefix(const std::string& path) {
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':';
}

bool has_unsafe_archive_character(const std::string& segment) {
    for (unsigned char c : segment) {
        if (c < 32)
            return true;
        switch (c) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            break;
        }
    }
    return false;
}

bool make_safe_relative_path(const std::string& archive_name,
                             fs::path& relative_out,
                             std::string& error_out) {
    std::string normalized = archive_name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty()) {
        error_out = "Archive contains an empty entry name";
        return false;
    }
    if (normalized.front() == '/' || has_windows_drive_prefix(normalized)) {
        error_out = "Archive contains an absolute entry path: " + archive_name;
        return false;
    }

    fs::path relative;
    std::size_t start = 0;
    bool has_segment = false;
    while (start <= normalized.size()) {
        const std::size_t slash = normalized.find('/', start);
        const std::size_t end =
            slash == std::string::npos ? normalized.size() : slash;
        const std::string segment = normalized.substr(start, end - start);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                error_out = "Archive contains a parent-directory entry: " +
                    archive_name;
                return false;
            }
            if (has_unsafe_archive_character(segment)) {
                error_out = "Archive contains an unsafe entry name: " +
                    archive_name;
                return false;
            }
            relative /= fs::u8path(segment);
            has_segment = true;
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }

    if (!has_segment) {
        error_out = "Archive contains an empty entry path: " + archive_name;
        return false;
    }
    relative_out = relative;
    return true;
}

bool is_within_directory(const fs::path& root, const fs::path& path) {
    const fs::path normalized_root = fs::absolute(root).lexically_normal();
    const fs::path normalized_path = fs::absolute(path).lexically_normal();
    auto root_it = normalized_root.begin();
    auto path_it = normalized_path.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
        if (path_it == normalized_path.end() || *root_it != *path_it)
            return false;
    }
    return true;
}

size_t write_zip_chunk(void* opaque,
                       mz_uint64 file_offset,
                       const void* data,
                       size_t size) {
    auto* context = static_cast<FileWriteContext*>(opaque);
    if (!context || !context->stream || !*context->stream)
        return 0;
    if (file_offset > static_cast<mz_uint64>(
            std::numeric_limits<std::streamoff>::max()))
        return 0;
    context->stream->seekp(static_cast<std::streamoff>(file_offset));
    context->stream->write(static_cast<const char*>(data),
                           static_cast<std::streamsize>(size));
    return *context->stream ? size : 0;
}

bool extract_file_to_path(mz_zip_archive& zip,
                          mz_uint index,
                          const fs::path& output_path,
                          std::string& error_out) {
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        error_out = "Cannot create extracted file: " + path_to_utf8(output_path);
        return false;
    }

    FileWriteContext context{&output};
    if (!mz_zip_reader_extract_to_callback(&zip, index, write_zip_chunk,
                                           &context, 0)) {
        error_out = "ZIP extraction failed: " + zip_error(zip);
        output.close();
        std::error_code ec;
        fs::remove(output_path, ec);
        return false;
    }

    output.close();
    if (!output) {
        error_out = "Cannot finish extracted file: " + path_to_utf8(output_path);
        std::error_code ec;
        fs::remove(output_path, ec);
        return false;
    }
    return true;
}

bool extract_zip_archive(const std::string& archive_body,
                         const std::string& destination,
                         std::string& error_out) {
    if (archive_body.empty()) {
        error_out = "Archive is empty";
        return false;
    }

    const fs::path destination_path = fs::u8path(destination);
    std::error_code ec;
    fs::create_directories(destination_path, ec);
    if (ec || !fs::is_directory(destination_path, ec)) {
        error_out = "Cannot create archive destination";
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, archive_body.data(),
                                archive_body.size(), 0)) {
        error_out = "Cannot read ZIP archive: " + zip_error(zip);
        return false;
    }

    const auto finish_with_error = [&](const std::string& message) {
        mz_zip_reader_end(&zip);
        error_out = message;
        return false;
    };

    const mz_uint file_count = mz_zip_reader_get_num_files(&zip);
    if (file_count == 0)
        return finish_with_error("Archive is empty or unreadable");
    if (file_count > kMaxZipEntryCount)
        return finish_with_error("Archive contains too many entries: " +
                                 std::to_string(file_count));

    mz_uint64 total_uncompressed_size = 0;
    for (mz_uint i = 0; i < file_count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            return finish_with_error("Cannot read ZIP entry metadata: " +
                                     zip_error(zip));

        fs::path relative_path;
        if (!make_safe_relative_path(stat.m_filename, relative_path, error_out)) {
            mz_zip_reader_end(&zip);
            return false;
        }

        const fs::path output_path = destination_path / relative_path;
        if (!is_within_directory(destination_path, output_path)) {
            return finish_with_error(
                "Archive entry would escape extraction directory: " +
                std::string(stat.m_filename));
        }

        if (stat.m_is_directory) {
            fs::create_directories(output_path, ec);
            if (ec)
                return finish_with_error("Cannot create archive directory: " +
                                         path_to_utf8(output_path));
            continue;
        }

        if (stat.m_is_encrypted)
            return finish_with_error(
                "Encrypted ZIP entries are not supported: " +
                std::string(stat.m_filename));
        if (!stat.m_is_supported)
            return finish_with_error(
                "Unsupported ZIP entry compression: " +
                std::string(stat.m_filename));
        if (stat.m_uncomp_size > kMaxZipEntryBytes)
            return finish_with_error(
                "ZIP entry is too large after decompression: " +
                std::string(stat.m_filename));
        if (stat.m_uncomp_size >
                kMaxZipTotalBytes - total_uncompressed_size) {
            return finish_with_error(
                "ZIP archive exceeds the decompressed size limit");
        }
        total_uncompressed_size += stat.m_uncomp_size;

        fs::create_directories(output_path.parent_path(), ec);
        if (ec)
            return finish_with_error("Cannot create archive directory: " +
                                     path_to_utf8(output_path.parent_path()));
        if (!extract_file_to_path(zip, i, output_path, error_out)) {
            mz_zip_reader_end(&zip);
            return false;
        }
    }

    mz_zip_reader_end(&zip);
    return true;
}

} // namespace

std::string sanitize_path_component(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_')
            result.push_back(static_cast<char>(c));
        else if (c == '.')
            result.push_back('_');
    }
    if (result.empty())
        result = "job";
    if (result.size() > kMaxSanitizedJobIdLength)
        result.resize(kMaxSanitizedJobIdLength);
    return result;
}

bool find_preferred_model_file(const std::string& directory,
                               std::string& model_path_out) {
    std::vector<std::string> files;
    easy3d::file_system::get_files(directory, files, true);
    std::sort(files.begin(), files.end(), [](const std::string& a,
                                             const std::string& b) {
        const std::string ea = easy3d::file_system::extension(a, true);
        const std::string eb = easy3d::file_system::extension(b, true);
        const int pa = model_extension_priority(ea);
        const int pb = model_extension_priority(eb);
        return pa == pb ? a < b : pa < pb;
    });

    for (const auto& file : files) {
        const std::string ext = easy3d::file_system::extension(file, true);
        if (!is_supported_model_extension(ext))
            continue;
        model_path_out = directory + "/" + file;
        return true;
    }
    return false;
}

bool save_generation_archive_and_find_model(const std::string& job_id,
                                            const std::string& archive_body,
                                            std::string& model_path_out,
                                            std::string& error_out) {
    const std::string output_dir = make_generation_output_dir(job_id);
    if (output_dir.empty()) {
        error_out = "Cannot create output directory";
        return false;
    }

    const std::string zip_path = output_dir + "/model.zip";
    if (!write_binary_file(zip_path, archive_body, error_out))
        return false;

    if (!extract_zip_archive(archive_body, output_dir, error_out))
        return false;

    if (!find_preferred_model_file(output_dir, model_path_out)) {
        error_out = "No supported model file in archive";
        return false;
    }
    return true;
}

} // namespace claw_3dgen
