// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_APP_UTIL_THREAD_PRIORITY_H
#define CLAW3D_APP_UTIL_THREAD_PRIORITY_H

/// Platform-tolerant helper for lowering background worker thread priority.

namespace claw3d::app {

void lower_current_thread_priority();

} // namespace claw3d::app

#endif // CLAW3D_APP_UTIL_THREAD_PRIORITY_H
