// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "io/surface_mesh_io.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/fileio/surface_mesh_io.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace claw3d::io {
namespace {

namespace fs = std::filesystem;

std::string path_to_utf8(const fs::path& path) {
#if defined(__cpp_char8_t)
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
#else
    return path.u8string();
#endif
}

std::string trim(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(),
        [](unsigned char ch) { return std::isspace(ch); });
    if (first == text.end())
        return {};
    const auto last = std::find_if_not(text.rbegin(), text.rend(),
        [](unsigned char ch) { return std::isspace(ch); }).base();
    return std::string(first, last);
}

bool starts_with_keyword(const std::string& line, const char* keyword) {
    const std::string key(keyword);
    if (line.size() < key.size() || line.compare(0, key.size(), key) != 0)
        return false;
    return line.size() == key.size() ||
        std::isspace(static_cast<unsigned char>(line[key.size()]));
}

std::vector<std::string> split_ws(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string token;
    while (in >> token)
        out.push_back(token);
    return out;
}

int material_option_argument_count(const std::string& option) {
    if (option == "-mm")
        return 2;
    if (option == "-o" || option == "-s" || option == "-t")
        return 3;
    if (option == "-blendu" || option == "-blendv" || option == "-boost" ||
        option == "-bm" || option == "-cc" || option == "-clamp" ||
        option == "-imfchan" || option == "-texres")
        return 1;
    return 0;
}

std::string extract_texture_name_from_map_kd(const std::string& payload) {
    const auto tokens = split_ws(payload);
    for (std::size_t i = 0; i < tokens.size();) {
        if (!tokens[i].empty() && tokens[i][0] == '-') {
            const int skip = material_option_argument_count(tokens[i]);
            i += 1 + static_cast<std::size_t>(skip);
            continue;
        }

        std::string texture_name = tokens[i++];
        while (i < tokens.size()) {
            texture_name += " ";
            texture_name += tokens[i++];
        }
        return texture_name;
    }
    return {};
}

bool has_windows_drive_prefix(const std::string& path) {
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) &&
        path[1] == ':';
}

bool has_parent_reference(const fs::path& path) {
    for (const auto& part : path) {
        if (part == "..")
            return true;
    }
    return false;
}

fs::path canonical_or_absolute(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (!ec && !normalized.empty())
        return normalized;
    ec.clear();
    normalized = fs::absolute(path, ec);
    return ec ? path.lexically_normal() : normalized.lexically_normal();
}

bool is_within_directory(const fs::path& root, const fs::path& candidate) {
    const fs::path root_path = canonical_or_absolute(root);
    const fs::path candidate_path = canonical_or_absolute(candidate);
    const fs::path relative = candidate_path.lexically_relative(root_path);
    if (relative.empty())
        return false;
    for (const auto& part : relative) {
        if (part == "..")
            return false;
    }
    return true;
}

bool resolve_contained_asset_path(const fs::path& root_dir,
                                  const std::string& base_file,
                                  const std::string& asset_name,
                                  std::string& path_out) {
    path_out.clear();
    if (asset_name.empty() || has_windows_drive_prefix(asset_name))
        return false;

    fs::path asset_path = fs::u8path(asset_name);
    if (asset_path.is_absolute() || has_parent_reference(asset_path))
        return false;

    const fs::path base_dir = fs::u8path(base_file).parent_path();
    const fs::path candidate =
        (base_dir.empty() ? asset_path : base_dir / asset_path).lexically_normal();
    if (!is_within_directory(root_dir, candidate))
        return false;

    path_out = path_to_utf8(candidate);
    return true;
}

std::string first_diffuse_texture_from_mtl(const std::string& mtl_path,
                                           const fs::path& root_dir) {
    std::ifstream in(mtl_path.c_str());
    if (!in)
        return {};

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (!starts_with_keyword(line, "map_Kd"))
            continue;

        const auto texture_name = extract_texture_name_from_map_kd(
            trim(line.substr(std::string("map_Kd").size())));
        std::string texture_path;
        if (!texture_name.empty() &&
            resolve_contained_asset_path(root_dir, mtl_path,
                                         texture_name, texture_path)) {
            return texture_path;
        }
    }
    return {};
}

void attach_obj_diffuse_texture_metadata(const std::string& file_name,
                                         easy3d::SurfaceMesh* mesh) {
    if (!mesh)
        return;

    std::ifstream in(file_name.c_str());
    if (!in)
        return;

    fs::path root_dir = fs::u8path(file_name).parent_path();
    if (root_dir.empty()) {
        std::error_code ec;
        root_dir = fs::current_path(ec);
        if (ec)
            root_dir = fs::path(".");
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (!starts_with_keyword(line, "mtllib"))
            continue;

        const std::string payload = trim(line.substr(std::string("mtllib").size()));
        if (payload.empty())
            continue;

        std::istringstream libs(payload);
        std::string lib_name;
        while (libs >> lib_name) {
            std::string mtl_path;
            if (!resolve_contained_asset_path(root_dir, file_name,
                                              lib_name, mtl_path)) {
                LOG(WARNING) << "OBJ material library rejected: path escapes model directory";
                continue;
            }
            const std::string texture_path =
                first_diffuse_texture_from_mtl(mtl_path, root_dir);
            if (!texture_path.empty()) {
                auto texture_prop = mesh->model_property<std::string>(
                    "m:texture_diffuse", "");
                texture_prop[0] = texture_path;
                LOG(INFO) << "OBJ diffuse texture metadata: " << texture_path;
                return;
            }
        }
    }
}

} // namespace

bool is_surface_mesh_file(const std::string& file_name) {
    const std::string ext = easy3d::file_system::extension(file_name, true);
    return ext == "ply" || ext == "obj" || ext == "stl" || ext == "off" ||
        ext == "sm" || ext == "glb" || ext == "gltf";
}

easy3d::SurfaceMesh* load_surface_mesh(const std::string& file_name) {
    const std::string ext = easy3d::file_system::extension(file_name, true);

    if (ext == "glb" || ext == "gltf") {
        auto mesh = std::make_unique<easy3d::SurfaceMesh>();
        mesh->set_name(file_name);
        if (!load_gltf_surface_mesh(file_name, mesh.get()))
            return nullptr;
        return mesh.release();
    }

    easy3d::SurfaceMesh* mesh = easy3d::SurfaceMeshIO::load(file_name);
    if (!mesh)
        return nullptr;

    if (ext == "obj")
        attach_obj_diffuse_texture_metadata(file_name, mesh);

    return mesh;
}

bool save_surface_mesh(const std::string& file_name,
                       const easy3d::SurfaceMesh* mesh) {
    return easy3d::SurfaceMeshIO::save(file_name, mesh);
}

} // namespace claw3d::io
