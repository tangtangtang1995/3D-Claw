// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#include "util/thread_priority.h"

#include "services/platform/platform_runtime.h"

namespace claw3d::app {

void lower_current_thread_priority()
{
    platform::lower_current_thread_priority();
}

} // namespace claw3d::app