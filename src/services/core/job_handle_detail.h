// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_SERVICES_JOB_HANDLE_DETAIL_H
#define CLAW3D_SERVICES_JOB_HANDLE_DETAIL_H

/// Internal helpers shared by value-type service job handles.

namespace claw3d::services::detail {

/// Small shared helpers for value-type job handles with private implementations.
template <typename ImplPtr>
auto runner_from(const ImplPtr& impl)
{
    return impl ? impl->runner : nullptr;
}

template <typename ImplPtr>
bool handle_valid(const ImplPtr& impl)
{
    return static_cast<bool>(runner_from(impl));
}

template <typename ImplPtr>
void reset_handle(ImplPtr& impl)
{
    impl.reset();
}

} // namespace claw3d::services::detail

#endif // CLAW3D_SERVICES_JOB_HANDLE_DETAIL_H
