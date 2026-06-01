// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_PLATFORM_RUNTIME_H
#define CLAW3D_SERVICES_PLATFORM_RUNTIME_H

/// Small cross-platform runtime probes and thread-priority helpers.

#include <cstdint>
#include <string>

namespace claw3d::platform {

/// Returns a short human-readable OS name for diagnostics and AI context.
std::string operating_system_name();
/// Returns total physical memory in MiB, or 0 when unavailable.
std::uint64_t total_physical_memory_mb();
/// Lowers the current worker thread priority where the platform supports it.
void lower_current_thread_priority();

} // namespace claw3d::platform

#endif // CLAW3D_SERVICES_PLATFORM_RUNTIME_H
