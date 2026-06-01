// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_PREVIEW_POLICY_H
#define CLAW3D_COMMON_PREVIEW_POLICY_H

/// Shared constants for throttling algorithm previews and snapshots.

namespace claw3d::preview_policy {

constexpr int kDefaultSnapshotMinMs = 80;
constexpr int kSlowPreviewUiMs = 240;
constexpr int kLargeMeshFaceCount = 100000;
constexpr int kHugeMeshFaceCount = 500000;
constexpr int kLargeMeshSnapshotMinMs = 250;
constexpr int kHugeMeshSnapshotMinMs = 500;
constexpr int kLargeTargetVertexCount = 50000;
constexpr int kDefaultMaxSnapshotFaces = 250000;
constexpr int kReducedMaxSnapshotFaces = 80000;

} // namespace claw3d::preview_policy

#endif // CLAW3D_COMMON_PREVIEW_POLICY_H
