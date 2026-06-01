// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_PRIMITIVE_PREVIEW_POLICY_H
#define CLAW3D_COMMON_PRIMITIVE_PREVIEW_POLICY_H

/// Shared constants for primitive preview sampling and patch sizing.

#include <cstddef>

namespace claw3d::primitive_preview_policy {

constexpr float kRansacPlanePatchBboxRatio = 0.01f;
constexpr float kRansacPlanePatchEpsilonMultiplier = 2.0f;
constexpr float kRegionGrowingPlanePatchBboxRatio = 0.015f;
constexpr int kRansacDialogInlierSampleLimit = 5000;
constexpr std::size_t kPrimitiveJobSampleLimit = 3000;

} // namespace claw3d::primitive_preview_policy

#endif // CLAW3D_COMMON_PRIMITIVE_PREVIEW_POLICY_H
