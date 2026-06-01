// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "services/platform/platform_runtime.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#else
#  include <unistd.h>
#endif

#include <cstddef>

namespace claw3d::platform {

std::string operating_system_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::uint64_t total_physical_memory_mb() {
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        return ms.ullTotalPhys / (1024ULL * 1024ULL);
#elif defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t size = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0)
        return bytes / (1024ULL * 1024ULL);
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        const auto bytes = static_cast<unsigned long long>(pages) *
                           static_cast<unsigned long long>(page_size);
        return bytes / (1024ULL * 1024ULL);
    }
#endif
    return 0;
}

void lower_current_thread_priority() {
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

} // namespace claw3d::platform