// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALGORITHM_ID_H
#define CLAW3D_ALGORITHM_ID_H

/// Stable algorithm identifiers and scene-result disposition policy.

/// Stable identifier for app-visible algorithm jobs.
enum class AlgorithmId {
    Unknown,
    PoissonReconstruction,
    AlphaWrap3D,
    RansacPrimitive,
    RegionGrowing,
    CgalSimplification,
    AcvdRemeshing,
    VsaApproximation,
    PlanarPatchRemeshing,
    CgalSmoothing,
    MeanCurvatureFlowSkeleton,
    GeodesicDistance,
    ArapDeformation,
    Parameterization,
    SurfaceMeshSampling,
    SurfaceMeshSimplification,
    SurfaceMeshSmoothing,
    SurfaceMeshFairing,
    SurfaceMeshHoleFilling,
    SurfaceMeshRemeshing,
    PointCloudNormalEstimation,
    ThreeDGeneration
};

/// Describes how completed backend models should be attached to the scene.
enum class ResultDisposition {
    AddAsChild,
    ReplaceSource,
    HideSourceAndAddChild,
    AddPrimitiveChildren
};

#endif // CLAW3D_ALGORITHM_ID_H
