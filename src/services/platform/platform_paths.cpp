// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/platform/platform_paths.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <crt_externs.h>
#    include <mach-o/dyld.h>
#  endif
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
extern "C" {
extern char** environ;
}
#endif

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <string>

namespace claw3d::platform {
namespace {

namespace fs = std::filesystem;

constexpr const char* kAppDirectoryName = "3DClaw";
constexpr const char* kCjkFontEnv = "CLAW3D_CJK_FONT_PATH";

std::string path_to_utf8(const fs::path& path) {
#if defined(__cpp_char8_t)
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
#else
    return path.u8string();
#endif
}

fs::path env_path(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value)
        return fs::path();
    return fs::u8path(value);
}

fs::path temp_root() {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec || dir.empty())
        dir = fs::current_path(ec);
    if (ec || dir.empty())
        dir = fs::path(".");
    return dir;
}

std::string ensure_directory(const fs::path& path) {
    if (path.empty())
        return std::string();
    std::error_code ec;
    if (!fs::exists(path, ec))
        fs::create_directories(path, ec);
    if (ec)
        return std::string();
    return path_to_utf8(path);
}

std::string first_existing_file(const fs::path candidates[], std::size_t count) {
    std::error_code ec;
    for (std::size_t i = 0; i < count; ++i) {
        if (!candidates[i].empty() && fs::is_regular_file(candidates[i], ec))
            return path_to_utf8(candidates[i]);
        ec.clear();
    }
    return std::string();
}

std::string detect_executable_directory() {
#if defined(_WIN32)
    std::wstring buffer(260, L'\0');
    for (;;) {
        const DWORD size = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            return std::string();
        if (size < buffer.size() - 1) {
            buffer.resize(size);
            return path_to_utf8(fs::path(buffer).parent_path());
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return std::string();
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return std::string();
    return path_to_utf8(fs::u8path(buffer.data()).parent_path());
#else
    for (std::size_t capacity = 4096; capacity <= 65536; capacity *= 2) {
        std::vector<char> buffer(capacity, '\0');
        const ssize_t size = readlink("/proc/self/exe", buffer.data(),
                                      buffer.size() - 1);
        if (size <= 0)
            return std::string();
        if (static_cast<std::size_t>(size) < buffer.size() - 1) {
            buffer[static_cast<std::size_t>(size)] = '\0';
            return path_to_utf8(fs::u8path(buffer.data()).parent_path());
        }
    }
    return std::string();
#endif
}

#ifndef _WIN32
char* const* process_environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return ::environ;
#endif
}

bool spawn_and_wait(const char* command,
                    const char* argv0,
                    const std::string& argument) {
    char* const argv[] = {
        const_cast<char*>(argv0),
        const_cast<char*>(argument.c_str()),
        nullptr
    };
    pid_t pid = 0;
    if (posix_spawnp(&pid, command, nullptr, nullptr, argv,
                     process_environment()) != 0)
        return false;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return true;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

} // namespace

std::string executable_directory() {
    static const std::string dir = detect_executable_directory();
    return dir;
}

std::string user_config_directory() {
#if defined(_WIN32)
    fs::path base = env_path("APPDATA");
    if (base.empty())
        base = env_path("LOCALAPPDATA");
    if (base.empty())
        base = env_path("USERPROFILE");
    if (base.empty())
        base = temp_root();
    return ensure_directory(base / kAppDirectoryName);
#elif defined(__APPLE__)
    fs::path home = env_path("HOME");
    if (home.empty())
        home = temp_root();
    return ensure_directory(home / "Library" / "Application Support" / kAppDirectoryName);
#else
    fs::path base = env_path("XDG_CONFIG_HOME");
    if (base.empty()) {
        fs::path home = env_path("HOME");
        base = home.empty() ? temp_root() : home / ".config";
    }
    return ensure_directory(base / kAppDirectoryName);
#endif
}

std::string user_cache_directory() {
#if defined(_WIN32)
    fs::path base = env_path("LOCALAPPDATA");
    if (base.empty())
        base = env_path("APPDATA");
    if (base.empty())
        base = env_path("USERPROFILE");
    if (base.empty())
        base = temp_root();
    return ensure_directory(base / kAppDirectoryName / "Cache");
#elif defined(__APPLE__)
    fs::path home = env_path("HOME");
    if (home.empty())
        home = temp_root();
    return ensure_directory(home / "Library" / "Caches" / kAppDirectoryName);
#else
    fs::path base = env_path("XDG_CACHE_HOME");
    if (base.empty()) {
        fs::path home = env_path("HOME");
        base = home.empty() ? temp_root() : home / ".cache";
    }
    return ensure_directory(base / kAppDirectoryName);
#endif
}

std::string user_data_directory() {
#if defined(_WIN32)
    fs::path base = env_path("LOCALAPPDATA");
    if (base.empty())
        base = env_path("APPDATA");
    if (base.empty())
        base = env_path("USERPROFILE");
    if (base.empty())
        base = temp_root();
    return ensure_directory(base / kAppDirectoryName);
#elif defined(__APPLE__)
    fs::path home = env_path("HOME");
    if (home.empty())
        home = temp_root();
    return ensure_directory(home / "Library" / "Application Support" /
                            kAppDirectoryName);
#else
    fs::path base = env_path("XDG_DATA_HOME");
    if (base.empty()) {
        fs::path home = env_path("HOME");
        base = home.empty() ? temp_root() : home / ".local" / "share";
    }
    return ensure_directory(base / kAppDirectoryName);
#endif
}

std::string temporary_directory() {
    return ensure_directory(temp_root() / kAppDirectoryName);
}

std::string generation_cache_directory() {
    const std::string cache = user_cache_directory();
    const fs::path base = cache.empty() ? fs::u8path(temporary_directory())
                                        : fs::u8path(cache);
    return ensure_directory(base / "ai_3d_generation");
}

std::string generated_models_directory() {
    const std::string executable_dir = executable_directory();
    if (!executable_dir.empty()) {
        const std::string output =
            ensure_directory(fs::u8path(executable_dir) / "generated_models");
        if (!output.empty())
            return output;
    }

    const std::string data = user_data_directory();
    const fs::path base = data.empty() ? fs::u8path(temporary_directory())
                                       : fs::u8path(data);
    return ensure_directory(base / "generated_models");
}

std::string app_config_path(const std::string& file_name) {
    const std::string dir = user_config_directory();
    if (dir.empty() || file_name.empty())
        return std::string();
    return path_to_utf8(fs::u8path(dir) / fs::u8path(file_name));
}

std::string locate_cjk_font() {
    const fs::path explicit_font = env_path(kCjkFontEnv);
    if (!explicit_font.empty()) {
        const fs::path candidates[] = {explicit_font};
        const std::string found = first_existing_file(candidates, 1);
        if (!found.empty())
            return found;
    }

#if defined(_WIN32)
    const fs::path candidates[] = {
        fs::path("C:/Windows/Fonts/msyh.ttc"),
        fs::path("C:/Windows/Fonts/msyh.ttf"),
        fs::path("C:/Windows/Fonts/simhei.ttf"),
        fs::path("C:/Windows/Fonts/simsun.ttc")
    };
#elif defined(__APPLE__)
    const fs::path candidates[] = {
        fs::path("/System/Library/Fonts/PingFang.ttc"),
        fs::path("/System/Library/Fonts/STHeiti Light.ttc"),
        fs::path("/System/Library/Fonts/STHeiti Medium.ttc"),
        fs::path("/Library/Fonts/Arial Unicode.ttf")
    };
#else
    const fs::path candidates[] = {
        fs::path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        fs::path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.otf"),
        fs::path("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"),
        fs::path("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"),
        fs::path("/usr/share/fonts/truetype/arphic/ukai.ttc")
    };
#endif
    return first_existing_file(candidates, sizeof(candidates) / sizeof(candidates[0]));
}

bool open_directory(const std::string& path) {
    if (path.empty())
        return false;
    const fs::path dir = fs::u8path(path);
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return false;

#if defined(_WIN32)
    const std::wstring wide = dir.wstring();
    HINSTANCE result = ShellExecuteW(
        nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#elif defined(__APPLE__)
    return spawn_and_wait("/usr/bin/open", "open", path_to_utf8(dir));
#else
    return spawn_and_wait("xdg-open", "xdg-open", path_to_utf8(dir));
#endif
}

} // namespace claw3d::platform
