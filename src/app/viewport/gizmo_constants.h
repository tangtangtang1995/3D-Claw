// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_APP_VIEWPORT_GIZMO_CONSTANTS_H
#define CLAW3D_APP_VIEWPORT_GIZMO_CONSTANTS_H

/// Tunable viewport gizmo sizes, hit radii, and drawing constants.

namespace claw3d::viewport_gizmo {

constexpr float kMinArrowLength = 0.05f;
constexpr float kTransformArrowLengthRatio = 0.35f;
constexpr float kCropArrowLengthRatio = 0.16f;
constexpr float kMinDragLineWidth = 1e-4f;
constexpr float kDragLineWidthRatio = 0.0025f;

} // namespace claw3d::viewport_gizmo

#endif // CLAW3D_APP_VIEWPORT_GIZMO_CONSTANTS_H
